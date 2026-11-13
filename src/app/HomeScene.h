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
    /// Tienda de mods de un juego: se entra desde el boton MODS.
    static constexpr i32 kModsSection    = 105;
    /// Ficha PUBLICA de un creador: foto, nombre, antiguedad y seguidores.
    /// Se abre al pulsar su tarjeta (no el boton SEGUIR, que solo alterna).
    static constexpr i32 kCreatorSection = 106;
    /// La comunidad de ese creador. Por ahora vacia.
    static constexpr i32 kCommunitySection = 107;
    /// Catalogo de CODIGOS de los juegos publicados. Se abre desde el boton
    /// JUEGOS, bajo GUARDAR ARCHIVOS.
    static constexpr i32 kGamesSection = 108;
    /// Seccion EULA propio: se entra desde el campo 10 de SUBIR JUEGO. Es una
    /// seccion aparte y no un panel porque los tres bloques de reglas piden
    /// toda la ventana para poder leerse y escribirse comodos.
    static constexpr i32 kEulaSection    = 102;
    /// Ficha de un producto: se entra pulsando su tarjeta en la Tienda.
    static constexpr i32 kProductSection = 103;
    /// Lista completa de un genero: se entra desde los botones del final de
    /// la Tienda.
    static constexpr i32 kGenreSection   = 104;
    /// NOTIFICACIONES: los comunicados que el creador del juego ha publicado.
    /// Se entra desde el detalle del juego en la biblioteca.
    static constexpr i32 kNoticesSection = 109;

    /// Ajustes de publicacion dentro de la mini-seccion PRECIOS: seis botones
    /// en dos columnas de tres. Cada uno abre una lista de opciones donde se
    /// elige un valor; el valor elegido se muestra en el propio boton.
    static constexpr i32 kPriceOptCount = 6;

    /// Campos del formulario de SUBIR JUEGO: cinco por lado, con la zona de
    /// subida de codigo entre las dos columnas.
    static constexpr i32 kUploadSlots = 10;

    /// Desplazamiento actual del panel de biblioteca (para pruebas).
    i32 libraryScrollOffset() const { return m_leftPanel.offset(); }
    /// Desplazamiento actual de la tienda (para pruebas).
    i32 storeScrollOffset() const   { return m_storePanel.offset(); }
    /// Desplaza la tienda, para dejar a la vista lo que se quiera comprobar.
    void scrollStoreForTest(i32 y)  { m_storePanel.setOffset(y); }
    /// Alto de todo lo que precede al catalogo en tarjetas (banner, tiras,
    /// carrusel, destacados, listas y creadores). Lo publica la escena para
    /// que las pruebas no tengan que repetir el calculo y desincronizarse.
    static i32 storeHeaderHeight();
    /// Alto de las dos rejillas de botones de genero, al final de la Tienda.
    static i32 storeGenresHeight();

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

    // --- accesores del Modo Desarrollador (para pruebas) ---
    Recti devColumnButtonRect(i32 i) const {
        return (i >= 0 && i < kDevButtons) ? m_devBtnRects[i] : Recti{};
    }
    /// Cuantos botones tiene la columna del Modo Desarrollador.
    static constexpr i32 devColumnButtonCount() { return kDevButtons; }
    Recti devPanelRect() const       { return m_devPanelRect; }
    i32   devOpenPanel() const       { return m_devPanel; }
    Recti priceTrackRect() const     { return m_priceTrackRect; }
    Recti priceKnobRect() const      { return m_priceKnobRect; }
    f32   priceT() const             { return m_priceT; }
    void  setPriceT(f32 t)           { m_priceT = t; }
    /// Precio actual en centavos de dolar, segun el deslizador.
    i32   priceUsdCents() const;
    Recti priceOptionRect(i32 i) const {
        return (i >= 0 && i < kPriceOptCount) ? m_priceOptRects[i] : Recti{};
    }
    i32   priceOptionOpen() const        { return m_priceOptOpen; }
    /// Rectangulo de la lista desplegada (vacio si no hay ninguna abierta).
    Recti priceOptionListRect() const    { return m_priceOptListRect; }
    /// Alto de cada fila de la lista desplegada.
    static constexpr i32 priceOptionRowHeight() { return 22; }
    i32   priceOptionSelected(i32 i) const {
        return (i >= 0 && i < kPriceOptCount) ? m_priceOptSel[i] : -1;
    }

    // --- accesores del formulario SUBIR JUEGO (para pruebas) ---
    Recti uploadSlotRect(i32 i) const {
        return (i >= 0 && i < kUploadSlots) ? m_upRects[i] : Recti{};
    }
    Recti uploadDropRect() const     { return m_upDropRect; }
    Recti uploadPanelRect() const    { return m_upPanelRect; }
    i32   uploadOpenSlot() const     { return m_upOpen; }

    // --- estado de la ficha (para pruebas y para la interfaz) ---
    Recti uploadDoneRect() const     { return m_upDoneRect; }
    /// true si los DIEZ campos del formulario estan puestos.
    bool  uploadFormComplete() const { return missingUploadSlot() < 0; }
    /// Primer campo sin rellenar, o -1 si no falta ninguno. El orden es el de
    /// UploadSlot, asi que el aviso nombra el que toca arreglar antes.
    i32   missingUploadSlot() const;
    /// Etiqueta de un campo, para decir cual falta.
    static const char* uploadSlotLabel(i32 i);
    /// true si la ficha se dio por terminada con TERMINE DE EDITAR.
    bool  uploadFormConfirmed() const { return m_upFichaLista; }
    /// true si PUBLICAR esta habilitado ahora mismo.
    bool  publishEnabled() const;
    /// Pulsa TERMINE DE EDITAR sin simular el raton.
    void  finishEditingForTest()     { finishUploadEditing(); }
    /// Rellena los DIEZ campos de la ficha de golpe, para no tener que
    /// simular diez interacciones en cada prueba que solo quiere llegar al
    /// paso siguiente. `nombre` vacio deja ese campo sin poner a proposito.
    void  fillUploadFormForTest(const std::string& nombre);
    /// Abre una mini-seccion del Modo Desarrollador sin simular el clic.
    void  openDevPanelForTest(i32 i)    { m_devPanel = i; }
    /// Abre uno de los diez campos del formulario de subida.
    void  openUploadSlotForTest(i32 i)  { m_upOpen = i; }
    void  openUploadSlot(i32 i)      { m_upOpen = (i >= 0 && i < kUploadSlots) ? i : -1; }
    bool  uploadDropHover() const    { return m_upDropHover; }
    void  setUploadDropHover(bool h) { m_upDropHover = h; }
    /// Fps de fabrica que marca el deslizador del campo 4.
    i32   uploadFps() const;
    /// Edad MINIMA que marca el primer carril del campo 5.
    i32   uploadAge() const;
    /// Edad MAXIMA, del segundo carril.
    i32   uploadAgeTop() const;
    void  setUploadFpsT(f32 t)       { m_upFpsT = t; }
    void  setUploadAgeT(f32 t)       { m_upAgeT = t; }
    void  setUploadAgeMaxT(f32 t)    { m_upAgeMaxT = t; }
    Recti uploadTrack2Rect() const   { return m_upTrack2Rect; }
    Recti uploadKnob2Rect() const    { return m_upKnob2Rect; }
    /// Color de fondo de la seccion EDAD para la edad marcada: cielo en el
    /// minimo, violeta intenso en el maximo.
    Color uploadAgeBackground() const;

    // --- las tres opciones de edad (para pruebas) ---
    static constexpr i32 ageOptionCount() { return kAgeOpts; }
    static constexpr i32 ageRangeCount()  { return kAgeRanges; }
    static constexpr i32 ageAiCount()     { return kAgeAiOpts; }
    /// Etiqueta del tramo `i`: "20 - 35 AÑOS".
    static const char* ageRangeLabel(i32 i);
    /// Edades del tramo `i`. Devuelve false si el indice no vale.
    static bool ageRangeValues(i32 i, i32& desde, i32& hasta);
    /// Etiqueta de la propuesta `i` del selector AI.
    static const char* ageAiLabel(i32 i);
    static bool ageAiValues(i32 i, i32& desde, i32& hasta);

    Recti ageOptionRect(i32 i) const {
        return (i >= 0 && i < kAgeOpts) ? m_ageOptRect[i] : Recti{};
    }
    Recti ageRangeRect(i32 i) const {
        return (i >= 0 && i < kAgeRanges) ? m_ageRangeRect[i] : Recti{};
    }
    Recti ageManualFieldRect() const { return m_ageManualRect; }
    Recti ageManualOkRect() const    { return m_ageManualOkRect; }
    Recti ageAiRect() const          { return m_ageAiRect; }
    i32   ageModeOpen() const        { return m_ageMode; }
    void  setAgeModeForTest(i32 m)   { m_ageMode = m; }
    i32   ageRangeSelected() const   { return m_ageRangeSel; }
    i32   ageAiPicked() const        { return m_ageAiPick; }
    void  setAgeManualText(const std::string& s) { m_ageManualField.setText(s); }
    /// Aplica la cifra escrita a mano. Devuelve false si no vale.
    bool  applyManualAge();
    /// Elige el tramo `i` y fija la edad a su extremo menor.
    void  pickAgeRange(i32 i);
    /// Lanza el sorteo del selector AI.
    void  rollAgeAi();
    const std::string& ageMessage() const { return m_ageMsg; }
    bool  ageMessageIsError() const       { return m_ageMsgErr; }
    Recti uploadTrackRect() const    { return m_upTrackRect; }
    Recti uploadKnobRect() const     { return m_upKnobRect; }
    const std::string& uploadName() const { return m_upNameField.text(); }
    void  setUploadNameForTest(const std::string& s) { m_upNameField.setText(s); }
    const std::string& uploadDesc() const { return m_upDescField.text(); }
    const std::string& uploadGenreText() const { return m_upGenreField.text(); }
    size_t uploadDescLength() const  { return m_upDescField.length(); }
    static constexpr size_t uploadDescMax() { return 1000; }
    /// Numero de generos disponibles para marcar.
    static i32 uploadGenreCount();
    /// Etiqueta del genero i, ya con su '#' delante.
    static std::string uploadGenreLabel(i32 i);
    const std::vector<i32>& uploadGenresChosen() const { return m_upGenres; }
    void  toggleUploadGenre(i32 i);
    const std::wstring& uploadCodePath() const { return m_upCodePath; }
    void  setUploadCodeForTest(const std::wstring& p) { m_upCodePath = p; }

    // --- campos 6 a 9 (para pruebas) ---
    /// Numero de modos de multijugador disponibles.
    static i32 uploadMultiCount();
    /// Etiqueta del modo i, ya con su '@' delante.
    static std::string uploadMultiLabel(i32 i);
    const std::vector<i32>& uploadMultiChosen() const { return m_upMulti; }
    void  toggleUploadMulti(i32 i);
    const std::string& uploadMultiText() const { return m_upMultiField.text(); }

    /// Respuesta a los dos avisos: 0 sin contestar, 1 acepta, 2 rechaza.
    i32   piracyConsent() const { return m_upPirateConsent; }
    i32   customConsent() const { return m_upCustomConsent; }
    void  setPiracyConsent(i32 c) { m_upPirateConsent = c; }
    void  setCustomConsent(i32 c) { m_upCustomConsent = c; }
    Recti consentYesRect() const  { return m_upYesRect; }
    Recti consentNoRect() const   { return m_upNoRect; }

    Recti promoArtDropRect() const { return m_upArtDropRect; }
    const std::wstring& promoArtPath() const { return m_upArtPath; }
    bool  promoArtLoaded() const   { return m_upArtW > 0 && m_upArtH > 0; }

    /// Boton PUBLICAR, al pie del formulario.
    Recti uploadPublishRect() const { return m_upPublishRect; }
    /// Boton GUARDAR Y SUBIR, bajo el del EULA.
    Recti uploadSaveRect() const    { return m_upSaveRect; }
    /// Boton ACEPTAR PRECIO, bajo la barrita de la mini-seccion PRECIOS.
    Recti priceAcceptRect() const   { return m_priceOkRect; }
    /// Segundos que le quedan al aviso "elige moneda y paises" (0 = sin aviso).
    f64   priceOptionsWarningLeft() const { return m_priceOptWarn; }
    // --- ACTUALIZAR JUEGO (para pruebas) ---
    //
    // La seccion ya no tiene contenido propio: es la de GUARDADO DE ARCHIVOS.
    // Sus rectangulos y su estado se consultan con los accesores `archive*`
    // de mas abajo; aqui solo quedan los que siguen teniendo sentido.
    /// -1 sin buscar, 0 no existe, 1 encontrado.
    i32   updateFound() const      { return m_updFound; }
    void  submitUpdateForTest()    { submitUpdate(); }
    const std::string& devMessage() const { return m_devMsg; }
    bool  devMessageIsError() const       { return m_devMsgErr; }
    /// Escribe el CODIGO, que es el unico campo que identifica el juego. Se
    /// conserva el nombre antiguo del accesor para las pruebas que ya lo usan.
    void  setUpdateName(const std::string& s) {
        m_arcCodeField.setText(s);
        lookupArchiveCode();
    }
    void  lookupUpdateGameForTest()           { lookupUpdateGame(); }
    /// Los tres botones de ACTUALIZAR: EULA, change log y comunicado.
    Recti updateEulaRect() const   { return m_updEulaRect; }
    Recti updateLogRect() const    { return m_updLogRect; }
    Recti updateNoticeRect() const { return m_updNoticeRect; }
    /// Cuadro abierto: -1 ninguno, 0 change log, 1 comunicado.
    i32   updateBoxOpen() const    { return m_updBoxOpen; }
    void  openUpdateBoxForTest(i32 i) { m_updBoxOpen = i; m_updNoticeFocus = 0; }
    const std::string& updateChangelog() const { return m_updLogField.text(); }
    void  setUpdateChangelog(const std::string& s) { m_updLogField.setText(s); }
    void  setNoticeForTest(const std::string& t, const std::string& b) {
        m_updNoticeTitle.setText(t);
        m_updNoticeBody.setText(b);
    }
    void  submitNoticeForTest() { submitNotice(); }
    // --- CREAR COMUNIDAD (para pruebas) ---
    static constexpr i32 communitySlotCount() { return kComSlots; }
    Recti communitySlotRect(i32 i) const {
        return (i >= 0 && i < kComSlots) ? m_comRects[i] : Recti{};
    }
    i32   communityOpenSlot() const   { return m_comOpen; }
    /// Franja del fondo que resume cuanto falta por rellenar.
    Recti communitySummaryRect() const { return m_comSummaryRect; }
    void  openCommunitySlot(i32 i)    { m_comOpen = (i >= 0 && i < kComSlots) ? i : -1; }
    std::string communitySlotValueForTest(i32 i) const { return communitySlotValue(i); }
    /// Nombre, descripcion y reglas.
    void  setCommunityName(const std::string& s)  { m_comNameField.setText(s); }
    void  setCommunityDesc(const std::string& s)  { m_comDescField.setText(s); }
    void  setCommunityRules(const std::string& s) { m_comRulesField.setText(s); }
    const std::string& communityName() const  { return m_comNameField.text(); }
    const std::string& communityDesc() const  { return m_comDescField.text(); }
    const std::string& communityRules() const { return m_comRulesField.text(); }
    /// Cuantos admins admite, y sus topes.
    i32   communityAdmins() const  { return m_comAdmins; }
    void  setCommunityAdmins(i32 n);
    /// Los demas ajustes, para comprobar sus mecanicas.
    i32   communityJoinMode() const { return m_comJoinMode; }
    i32   communityRankCount() const { return m_comRankCount; }
    i32   communityRankHours() const { return m_comRankHours; }
    i32   communityPunish(i32 tramo) const {
        return (tramo >= 0 && tramo < 3) ? m_comPunish[tramo] : -1;
    }
    i32   communityEventCount() const { return static_cast<i32>(m_comEvents.size()); }
    void  toggleCommunityEventForTest(i32 i);
    i32   communityColor() const { return m_comColor; }
    i32   communityBadge() const { return m_comBadge; }

    /// Boton NOTIFICACIONES del detalle de la biblioteca.
    Recti noticesRect() const { return m_noticesRect; }
    /// Cuantos comunicados ve el juego seleccionado.
    i32   noticeCount() { ensureNoticesLoaded(); return static_cast<i32>(m_notices.size()); }
    void  invalidateNoticesForTest() { m_noticesLoaded = false; }

    // --- ELIMINAR JUEGO (para pruebas) ---
    Recti deleteFieldRect() const { return m_delFieldRect; }
    Recti deleteYesRect() const   { return m_delYesRect; }
    Recti deleteNoRect() const    { return m_delNoRect; }
    /// -1 sin escribir, 0 ningun juego tuyo tiene ese codigo, 1 resuelto.
    i32   deleteFound() const     { return m_delFound; }
    /// Escribe el CODIGO del juego a retirar. Se conserva el nombre antiguo
    /// del accesor para las pruebas que ya lo usan.
    void  setDeleteName(const std::string& s) { m_delCodeField.setText(s); }
    /// Juego al que apunta el codigo escrito (vacio si no resuelve).
    const std::string& deleteGame() const     { return m_delJuego; }
    void  lookupDeleteGameForTest()           { lookupDeleteGame(); }
    void  confirmDeleteForTest()              { confirmDeleteGame(); }
    const std::string& deleteMessage() const  { return m_delMsg; }
    /// Cuantas lineas tiene el aviso que se lee antes de retirar.
    static i32 deleteNoticeLines();

    // --- LudoUpdates (para pruebas) ---
    static constexpr i32 ludoBlockCount() { return kLudoBlocks; }
    Recti ludoRect() const { return m_ludoRect; }
    Recti ludoBlockRect(i32 i) const {
        return (i >= 0 && i < kLudoBlocks) ? m_ludoBlockRect[i] : Recti{};
    }
    /// Titulo de un bloque. Es el definitivo; el cuerpo es provisional.
    static const char* ludoBlockTitle(i32 i);
    /// Cuantas lineas de cuerpo tiene un bloque.
    static i32 ludoBlockLines(i32 i);
    static const char* ludoBlockLine(i32 i, i32 linea);
    i32   ludoScrollOffset() const  { return m_ludoPanel.offset(); }
    void  scrollLudoForTest(i32 y)  { m_ludoPanel.setOffset(y); }
    i32   ludoContentHeight() const { return m_ludoPanel.contentHeight(); }

    // --- GUARDAR ARCHIVOS (para pruebas) ---
    Recti archiveCodeRect() const  { return m_arcCodeRect; }
    Recti archiveGenRect() const   { return m_arcGenRect; }
    Recti archivePickRect() const  { return m_arcPickRect; }
    Recti archiveSaveRect() const  { return m_arcSaveRect; }
    /// Escribe el codigo y resuelve a que juego pertenece, como al teclearlo.
    void  setArchiveCode(const std::string& s) {
        m_arcCodeField.setText(s);
        lookupArchiveCode();
    }
    const std::string& archiveCode() const      { return m_arcCodeField.text(); }
    /// Juego al que apunta el codigo escrito ("" si no cuadra con ninguno).
    const std::string& archiveCodeGame() const  { return m_arcCodeJuego; }
    /// true si GENERAR CODIGO esta disponible (no hay juegos publicados).
    bool  canGenerateCode() const;
    void  pressGenerateCodeForTest()            { generateGameCode(); }
    const std::string& generatedCode() const    { return m_arcGenerado; }
    /// Finge la carpeta ya elegida: el dialogo de Windows no se puede abrir
    /// desde una prueba, pero todo lo que va despues si se puede medir.
    void  setArchiveFolderForTest(const std::wstring& dir, i32 files, u64 bytes) {
        m_arcFolder = dir; m_arcFiles = files; m_arcBytes = bytes;
    }
    void  startArchiveForTest()    { startArchive(); }
    /// Carpeta elegida que queda pendiente de guardar ("" si no hay).
    const std::wstring& archiveFolderForTest() const { return m_arcFolder; }
    void  pollArchiveForTest()     { pollArchive(); }
    bool  archiveRunning() const   { return m_archive.running(); }
    const std::string& archiveMessage() const { return m_arcMsg; }
    bool  archiveMessageIsError() const       { return m_arcMsgErr; }

    /// Boton MANTENER PRECIOS POR DEFECTO y sus dos mini-botones.
    Recti priceDefaultsRect() const    { return m_priceDefRect; }
    Recti priceDefaultsYesRect() const { return m_priceDefYes; }
    Recti priceDefaultsNoRect() const  { return m_priceDefNo; }
    bool  priceDefaultsOpen() const    { return m_priceDefOpen; }
    /// Da por elegidos los seis ajustes con su valor actual.
    void  acceptDefaultPriceOptions();

    // --- PRECIO A ESCRITO (para pruebas) ---
    Recti priceTypedRect() const      { return m_priceTypeRect; }
    Recti priceTypedFieldRect() const { return m_priceTypeFieldRect; }
    Recti priceTypedOkRect() const    { return m_priceTypeOkRect; }
    bool  priceTypedOpen() const      { return m_priceTypeOpen; }
    void  setPriceTypedOpen(bool on)  { m_priceTypeOpen = on; }
    void  setPriceTypedText(const std::string& s) { m_priceTypeField.setText(s); }
    const std::string& priceTypedMessage() const { return m_priceTypeMsg; }
    /// Toma la cifra escrita y la lleva al deslizador. Devuelve false si no
    /// es un numero valido.
    bool  applyTypedPrice();
    /// true mientras corre la palomita de precio aceptado.
    bool  priceAcceptedAnimating() const  { return m_priceOkAnimOn; }
    /// Marca un ajuste como elegido (para pruebas).
    void  setPriceOptionChosen(i32 i, i32 v) {
        if (i >= 0 && i < kPriceOptCount) { m_priceOptSel[i] = v; m_priceOptSet[i] = true; }
    }
    /// true si moneda y pais estan elegidos, que es lo que exige aceptar.
    bool  priceOptionsReady() const;
    /// Segundos que le quedan al aviso bajo el boton de subir (0 = sin aviso).
    f64   priceWarningLeft() const  { return m_upPriceWarn; }
    /// true si el aviso en curso es de exito (verde) en vez de error (rojo).
    bool  uploadWarningIsOk() const { return m_upWarnOk; }
    /// Que error avisa: 0 falta el precio, 1 falta el codigo.
    i32   uploadWarningKind() const { return m_upWarnKind; }
    /// true si el precio ya se dio por bueno con ACEPTAR PRECIO.
    bool  priceAccepted() const     { return m_priceAccepted; }
    /// Juegos que la Tienda muestra: los publicados, en orden de catalogo.
    std::vector<std::string> storeGameNames() const;

    // --- tarjetas y ficha del producto (para pruebas) ---
    /// Numero de tarjetas dibujadas en la Tienda.
    i32   storeCardCount() const { return static_cast<i32>(m_storeCards.size()); }
    Recti storeCardRect(i32 i) const {
        return (i >= 0 && i < static_cast<i32>(m_storeCards.size()))
             ? m_storeCards[static_cast<size_t>(i)].rect : Recti{};
    }
    i32   storeCardGame(i32 i) const {
        return (i >= 0 && i < static_cast<i32>(m_storeCards.size()))
             ? m_storeCards[static_cast<size_t>(i)].gameIndex : -1;
    }
    // --- secciones 1 y 2 de la Tienda (para pruebas) ---
    Recti storeBannerRect() const  { return m_bannerRect; }
    i32   storeBannerIndex() const { return m_bannerIndex; }
    /// Segundos que el banner tarda en pasar al juego siguiente.
    static constexpr f64 storeBannerPeriod() { return 300.0; }   // 5 minutos
    Recti storeStripRect(i32 i) const {
        return (i >= 0 && i < kStripCount) ? m_stripRect[i] : Recti{};
    }
    static constexpr i32 storeStripCount() { return 3; }
    /// Juegos de la mini-seccion i (0 proximamente, 1 ofertas, 2 nuevos).
    std::vector<i32> storeStripGames(i32 i) const { return stripGames(i); }

    // --- secciones 3 a 6 (para pruebas) ---
    Recti carouselRect() const       { return m_carRect; }
    Recti carouselArrowLeft() const  { return m_carArrowL; }
    Recti carouselArrowRight() const { return m_carArrowR; }
    i32   carouselIndex() const      { return m_carIndex; }
    /// true mientras la imagen se esta deslizando.
    bool  carouselSliding() const    { return m_carDir != 0; }
    /// Mueve el carrusel como si se pulsara una flecha.
    void  carouselStep(i32 dir);

    Recti topRatedRect() const       { return m_topRect; }
    Recti topRatedArrowLeft() const  { return m_topArrowL; }
    Recti topRatedArrowRight() const { return m_topArrowR; }
    /// Orden de los juegos por valoracion, del mejor al peor.
    const std::vector<i32>& topRatedOrder() const { return m_topOrder; }
    void  refreshTopRatedForTest()   { refreshTopRated(); }
    /// Cada cuanto se renueva el orden por valoracion.
    static constexpr f64 topRatedPeriod() { return 86400.0; }   // 24 horas

    // --- secciones de generos (para pruebas) ---
    /// Cuantos botones de genero se dibujaron.
    i32   genreButtonCount() const { return static_cast<i32>(m_genreBtns.size()); }
    Recti genreButtonRect(i32 i) const {
        return (i >= 0 && i < static_cast<i32>(m_genreBtns.size()))
             ? m_genreBtns[static_cast<size_t>(i)] : Recti{};
    }
    /// Y de la segunda lista, la de combinaciones.
    i32   comboButtonCount() const { return static_cast<i32>(m_comboBtns.size()); }
    Recti comboButtonRect(i32 i) const {
        return (i >= 0 && i < static_cast<i32>(m_comboBtns.size()))
             ? m_comboBtns[static_cast<size_t>(i)] : Recti{};
    }
    /// Genero abierto (-1 = ninguno) y su pareja si es combinacion.
    i32   openGenre() const  { return m_genreOpen; }
    i32   openGenre2() const { return m_genreOpen2; }
    /// Abre la seccion de un genero, o de una combinacion si g2 >= 0.
    void  openGenreSection(i32 g1, i32 g2 = -1);
    /// Juegos que saldrian en esa seccion, del peor al mejor valorado.
    std::vector<i32> genreGames(i32 g1, i32 g2 = -1) const {
        return gamesOfGenre(g1, g2);
    }

    Recti creatorCardRect() const    { return m_creatorRect; }
    Recti followButtonRect() const   { return m_followRect; }

    // --- ficha publica del creador (para pruebas) ---
    const std::string& creatorOpen() const { return m_creatorOpen; }
    void  openCreatorForTest(const std::string& correo) {
        m_creatorOpen = correo;
        m_section     = kCreatorSection;
    }
    Recti creatorBackRect() const    { return m_creatorBackRect; }
    Recti creatorJoinRect() const    { return m_creatorJoinRect; }
    Recti communityBackRect() const  { return m_communityBackRect; }
    i32   followerCountForTest(const std::string& e) const {
        return followerCount(e);
    }
    bool  followingCreator(const std::string& e) const { return isFollowing(e); }
    void  toggleFollowForTest(const std::string& e);

    i32   storeHovered() const   { return m_storeHover; }
    void  setStoreHovered(i32 i) { m_storeHover = i; }
    /// Juego abierto en la ficha (-1 = ninguno).
    i32   productOpen() const    { return m_storeOpen; }
    void  openProductForTest(i32 idx) { openProduct(idx); }
    Recti productArtRect() const     { return m_prodArtRect; }
    /// Boton DESCARGAR de la ficha, dentro de la imagen abajo a la derecha.
    Recti productBuyRect() const     { return m_prodBuyRect; }
    Recti productSideRect() const    { return m_prodSideRect; }
    Recti productReviewsRect() const { return m_prodReviewsPanel.rect(); }
    Recti productTabRect(i32 i) const {
        return (i >= 0 && i < 2) ? m_prodTabRect[i] : Recti{};
    }
    i32   productTab() const     { return m_prodTab; }
    void  setProductTab(i32 t)   { if (t == 0 || t == 1) m_prodTab = t; }
    /// Alto que se le pide a la franja derecha.
    static constexpr i32 productSideHeight() { return 800; }
    /// Lanza la publicacion como si se hubiera pulsado el boton (pruebas).
    void  publishUpload()           { submitUpload(); }
    /// Ficha que se guardaria ahora mismo (pruebas).
    devcatalog::Listing uploadListing() const { return buildListing(); }

    // --- EULA propio (para pruebas) ---
    static constexpr i32 eulaBlockCount() { return 3; }
    Recti eulaBlockRect(i32 i) const {
        return (i >= 0 && i < kEulaBlocks) ? m_eulaRects[i] : Recti{};
    }
    i32   eulaFocus() const        { return m_eulaFocus; }
    void  setEulaFocus(i32 i)      { if (i >= 0 && i < kEulaBlocks) m_eulaFocus = i; }
    const std::string& eulaText(i32 i) const;
    void  setEulaText(i32 i, const std::string& s);
    Recti eulaCloseRect() const    { return m_eulaCloseRect; }
    /// Titulo de cada bloque, para las pruebas y el dibujado.
    static const char* eulaBlockTitle(i32 i);
    static constexpr i32 uploadFpsMin() { return 24; }
    static constexpr i32 uploadFpsMax() { return 2000; }
    static constexpr i32 uploadAgeMin() { return 4; }
    static constexpr i32 uploadAgeMax() { return 90; }
    /// Etiqueta del valor elegido en un ajuste (para pruebas).
    const char* priceOptionValue(i32 i) const;
    static constexpr i32 priceOptionCount() { return kPriceOptCount; }

    // --- accesores de las secciones de la franja derecha (para pruebas) ---
    Recti priceTimelineRect() const   { return m_priceTimelineRect; }
    Recti versionHistoryRect() const  { return m_versionHistRect; }
    Recti playButtonRect() const      { return m_playBtnRect; }
    Recti reviewsButtonRect() const   { return m_reviewsBtnRect; }
    /// Botones DESINSTALAR/REINSTALAR y MODS, bajo JUGAR.
    Recti uninstallButtonRect() const { return m_uninstallRect; }
    Recti modsButtonRect() const      { return m_modsRect; }
    /// true si ese juego esta desinstalado (bloqueado, sin borrar nada).
    /// Carga la lista de disco la primera vez que se consulta.
    bool  isUninstalled(const std::string& game) const;

    // --- dialogo de desinstalar (para pruebas) ---
    bool  uninstallAskOpen() const  { return m_uninstAsk; }
    f64   uninstallAskTime() const  { return m_uninstAskTime; }
    /// Cuanto tarda en aparecer la botonera, en segundos.
    static constexpr f64 uninstallButtonDelay() { return 1.0; }
    Recti uninstallAskYesRect() const   { return m_uninstYesRect; }
    Recti uninstallAskCloudRect() const { return m_uninstCloudRect; }
    Recti uninstallAskNoRect() const    { return m_uninstNoRect; }
    /// Abre el dialogo sobre un juego concreto (pruebas).
    void  openUninstallAskForTest(const std::string& g) {
        m_uninstAsk = true; m_uninstAskTime = 0.0; m_uninstTarget = g;
    }
    void  setUninstalledForTest(const std::string& g, bool on) {
        setUninstalled(g, on);
    }
    /// Ancho de cada franja lateral de la Biblioteca para un ancho de
    /// framebuffer dado. 0 = no caben y el detalle ocupa todo.
    static i32 librarySideWidthFor(i32 fbWidth);
    /// Ancho que le queda al detalle central con ese framebuffer.
    static i32 libraryDetailWidthFor(i32 fbWidth) {
        return fbWidth - 2 * librarySideWidthFor(fbWidth);
    }
    /// Selecciona un juego de la biblioteca por indice (para pruebas).
    void  selectLibraryGame(i32 i)    { m_librarySelected = i; m_historyLoaded = false; }
    const history::GameHistory& loadedHistory() const { return m_history; }
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
    /// Seccion 1: banner grande que rota entre los juegos del catalogo.
    void drawStoreBanner(Framebuffer& fb, Recti area);
    /// Seccion 2: tres mini-secciones (proximamente, ofertas, recien
    /// publicados) bajo el banner.
    void drawStoreStrips(Framebuffer& fb, Recti area, i32 yBase);
    /// Carga la imagen del banner si cambio el juego que muestra.
    void ensureBannerLoaded();
    /// Juegos que entran en cada mini-seccion, por indice de catalogo.
    std::vector<i32> stripGames(i32 strip) const;

    /// Seccion 3: carrusel de promociones con flechas a los lados.
    void drawStoreCarousel(Framebuffer& fb, Recti area);
    /// Seccion 4: destacados, con imagen a un lado y resenas al otro.
    void drawStoreTopRated(Framebuffer& fb, Recti area);
    /// Seccion 5: listas largas de ofertas y mejor valorados.
    void drawStoreLists(Framebuffer& fb, Recti area, i32 yBase);
    /// Seccion 6: creadores a los que seguir, al final del scroll.
    void drawStoreCreators(Framebuffer& fb, Recti area, i32 yBase);
    /// Secciones 8 y 9: rejilla de botones por genero y por combinacion.
    /// Devuelve la Y en la que termina, para lo que venga despues.
    i32  drawStoreGenres(Framebuffer& fb, Recti area, i32 yBase);
    /// Seccion propia de un genero: todos sus juegos, del peor al mejor.
    void drawGenreSection(Engine& engine, Framebuffer& fb);
    void updateGenreSection(Engine& engine, f64 dt);

    /// Ficha publica de un creador y su comunidad.
    void drawCreatorSection(Engine& engine, Framebuffer& fb);
    void updateCreatorSection(Engine& engine, f64 dt);
    void drawCommunitySection(Engine& engine, Framebuffer& fb);
    void updateCommunitySection(Engine& engine, f64 dt);
    /// Cuantos siguen a ese creador. Hoy solo cuenta la cuenta local.
    i32  followerCount(const std::string& email) const;
    /// Juegos de un genero (o de la combinacion de dos), ya ordenados.
    std::vector<i32> gamesOfGenre(i32 g1, i32 g2) const;
    /// Flecha triangular de un carrusel. `derecha` invierte el sentido.
    void drawCarouselArrow(Framebuffer& fb, Recti r, bool derecha,
                           bool hover) const;
    /// Avanza las animaciones de deslizamiento de los dos carruseles.
    void updateCarousels(f64 dt);
    /// Recalcula el orden por valoracion (cada 24 h, o al forzarlo).
    void refreshTopRated();
    /// Carga los creadores seguidos desde disco, si aun no se hizo.
    void ensureFollowingLoaded();
    /// Guarda la lista de seguidos.
    void saveFollowing() const;
    /// true si se sigue a ese creador.
    bool isFollowing(const std::string& email) const;
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

    // --- tarjetas de la Tienda: hit-test y ficha del producto ---
    /// Una tarjeta dibujada: su rectangulo y que juego representa. Se llena
    /// al dibujar, porque la posicion depende del scroll, y se consulta al
    /// frame siguiente para el hover y el clic.
    struct StoreCard {
        Recti rect{};
        i32   gameIndex = -1;   // indice en m_storeGames; -1 = hueco generico
    };
    std::vector<StoreCard> m_storeCards;

    // --- seccion 1: banner de noticias y ofertas ---
    /// Rota entre los juegos del catalogo cada kBannerPeriod segundos. Sin
    /// red no hay noticias que traer, asi que el banner promociona lo que
    /// hay: cada juego publicado, por turnos, con su imagen.
    i32   m_bannerIndex = 0;
    f64   m_bannerTime  = 0.0;
    Recti m_bannerRect{};
    /// Imagen del juego que muestra el banner ahora mismo.
    std::vector<u32> m_bannerPixels;
    i32   m_bannerW = 0, m_bannerH = 0;
    std::string m_bannerOf;

    // --- seccion 2: tres mini-secciones bajo el banner ---
    /// Cada una filtra el catalogo de una forma distinta.
    enum StoreStrip { kStripSoon = 0, kStripOffers = 1, kStripNew = 2,
                      kStripCount = 3 };
    Recti m_stripRect[kStripCount]{};

    // --- seccion 3: carrusel con flechas a los lados ---
    /// Deslizamiento entre imagenes: `m_carIndex` es la actual y `m_carSlide`
    /// va de -1 a 1 mientras la animacion corre (0 = asentado). El signo dice
    /// hacia donde se mueve.
    i32   m_carIndex = 0;
    f32   m_carSlide = 0.0f;
    i32   m_carDir   = 0;        // -1 atras, +1 adelante, 0 quieto
    Recti m_carRect{};
    Recti m_carArrowL{}, m_carArrowR{};
    bool  m_carArrowLHover = false, m_carArrowRHover = false;

    // --- seccion 4: destacados y mejor valorados ---
    /// Misma mecanica de flechas, pero con imagen a la izquierda y texto con
    /// descripcion y resenas a la derecha.
    i32   m_topIndex = 0;
    f32   m_topSlide = 0.0f;
    i32   m_topDir   = 0;
    Recti m_topRect{};
    Recti m_topArrowL{}, m_topArrowR{};
    bool  m_topArrowLHover = false, m_topArrowRHover = false;
    /// Orden de los juegos por valoracion, recalculado cada 24 h.
    std::vector<i32> m_topOrder;
    f64   m_topRefresh = 0.0;

    /// Imagenes de las dos secciones con carrusel, cargadas por juego.
    std::vector<u32> m_carPixels;
    i32   m_carW = 0, m_carH = 0;
    std::string m_carOf;
    std::vector<u32> m_topPixels;
    i32   m_topW = 0, m_topH = 0;
    std::string m_topOf;

    // --- secciones 8 y 9: generos y combinaciones ---
    /// Cada boton abre una seccion propia con TODOS los juegos de ese genero,
    /// ordenados del peor al mejor valorado. Hoy estan vacias porque aun no
    /// hay juegos etiquetados; la maquetacion ya esta lista para cuando los
    /// haya.
    std::vector<Recti> m_genreBtns;      // un rectangulo por boton de genero
    std::vector<Recti> m_comboBtns;      // y por combinacion de dos generos
    i32   m_genreHover = -1;             // boton bajo el raton (-1 = ninguno)
    bool  m_genreHoverCombo = false;     // si el de arriba es de la 2a lista
    /// Genero abierto: indice en kUpGenres, o -1. Para las combinaciones se
    /// guardan los dos.
    i32   m_genreOpen  = -1;
    i32   m_genreOpen2 = -1;             // segundo genero si es combinacion
    ScrollPanel m_genrePanel;            // lista del genero abierto
    Recti m_genreBackRect{};
    bool  m_genreBackHover = false;

    // --- seccion 6: creadores a los que seguir ---
    Recti m_creatorRect{};
    Recti m_followRect{};
    bool  m_followHover = false, m_followDown = false;
    /// Creadores seguidos, por correo. Se persiste en disco.
    std::vector<std::string> m_following;
    bool  m_followingLoaded = false;
    /// Foto del creador que se muestra, cargada una vez.
    std::vector<u32> m_creatorPhoto;
    i32   m_creatorPhotoW = 0, m_creatorPhotoH = 0;
    bool  m_creatorPhotoTried = false;
    /// Tarjeta del creador bajo el raton (aparte del boton SEGUIR).
    bool  m_creatorHover = false, m_creatorDown = false;

    // --- ficha PUBLICA de un creador ---
    //
    // Se abre al pulsar su tarjeta. El boton SEGUIR sigue alternando el
    // seguimiento sin abrir nada: son dos acciones distintas sobre la misma
    // tarjeta, y mezclarlas haria imposible seguir a alguien sin salir de la
    // tienda.
    std::string m_creatorOpen;        // correo del creador mostrado
    Recti m_creatorBackRect{};        // volver a la tienda
    bool  m_creatorBackHover = false;
    Recti m_creatorJoinRect{};        // UNIRSE A COMUNIDAD
    bool  m_creatorJoinHover = false, m_creatorJoinDown = false;
    Recti m_communityBackRect{};      // volver desde la comunidad
    bool  m_communityBackHover = false;

    i32  m_storeHover    = -1;   // tarjeta bajo el raton
    i32  m_storeDown     = -1;
    /// Juego abierto en la ficha del producto (-1 = ninguno).
    i32  m_storeOpen     = -1;

    // Catalogo que muestra la Tienda: los juegos publicados por el usuario.
    // Se recarga cuando se publica uno, no cada frame -- leerlo al dibujar
    // seria tocar disco 60 veces por segundo.
    std::vector<devcatalog::Game> m_storeGames;
    bool m_storeLoaded = false;

    // Instalacion del juego que se este viendo en la tienda.
    Recti       m_installBtnRect{};
    bool        m_installHover = false;
    bool        m_installDown  = false;
    GameInstall m_install;
    bool        m_installStarted = false;
    /// Juego que se esta instalando y su precio, para registrarlo en la
    /// biblioteca cuando termine. Sin esto habria que adivinar cual era.
    std::string m_installingName;
    i32         m_installingCents = 0;
    std::vector<library::Entry> m_libraryGames;   // juegos ya instalados
    bool        m_libraryLoaded = false;
    i32         m_libraryHover  = -1;   // casilla de biblioteca bajo el raton
    i32         m_librarySelected = -1; // juego mostrado en el panel de detalle

    // Secciones de la franja derecha de la biblioteca.
    Recti m_priceTimelineRect{};   // linea de tiempo del precio
    Recti m_versionHistRect{};     // historial de versiones

    // Panel de detalle del juego (zona central de la biblioteca).
    Recti m_playBtnRect{};
    Recti m_reviewsBtnRect{};
    bool  m_playHover = false, m_playDown = false;

    // --- DESINSTALAR / REINSTALAR y MODS, bajo JUGAR ---
    /// Desinstalar NO borra los archivos: bloquea el acceso al juego y lo
    /// deja en penumbra. Asi reinstalar es inmediato y el progreso sigue
    /// intacto, que es justo lo que se pidio.
    Recti m_uninstallRect{};
    bool  m_uninstallHover = false, m_uninstallDown = false;
    Recti m_modsRect{};
    bool  m_modsHover = false, m_modsDown = false;

    // --- NOTIFICACIONES, bajo DESINSTALAR y MODS ---
    /// Los comunicados que el creador del juego ha publicado. El boton lleva
    /// un contador cuando hay alguno sin leer: sin eso, un aviso publicado
    /// pasaria desapercibido y no serviria de nada.
    Recti m_noticesRect{};
    bool  m_noticesHover = false, m_noticesDown = false;
    /// Comunicados del juego seleccionado, del mas nuevo al mas viejo.
    std::vector<notices::Notice> m_notices;
    bool  m_noticesLoaded = false;
    /// Juego cuyos comunicados estan cargados, para releer al cambiar.
    std::string m_noticesGame;
    ScrollPanel m_noticesPanel;
    /// Imagen del comunicado que se esta mirando, decodificada al abrirlo.
    std::vector<u32> m_noticeImgPix;
    i32   m_noticeImgW = 0, m_noticeImgH = 0;
    std::wstring m_noticeImgLoaded;   // de que archivo salio, para no recargar

    // --- confirmacion de desinstalar ---
    /// Desinstalar borra el progreso, asi que se pregunta antes. Los botones
    /// aparecen UN SEGUNDO despues del aviso: obliga a leerlo en vez de
    /// pulsar por inercia donde estaba el boton anterior.
    bool  m_uninstAsk = false;      // dialogo abierto
    f64   m_uninstAskTime = 0.0;    // segundos desde que se abrio
    std::string m_uninstTarget;     // juego que se va a borrar
    Recti m_uninstPanelRect{};
    Recti m_uninstYesRect{}, m_uninstCloudRect{}, m_uninstNoRect{};
    i32   m_uninstHoverBtn = -1;    // 0 aceptar, 1 nube, 2 rechazar
    i32   m_uninstDownBtn  = -1;

    /// Juegos desinstalados (por nombre). Se persiste en disco.
    /// `mutable` porque la lista se carga perezosamente la primera vez que se
    /// consulta, y consultarla no cambia el estado observable del objeto.
    mutable std::vector<std::string> m_uninstalled;
    mutable bool m_uninstalledLoaded = false;

    // Juego en ejecucion: handle del proceso y animacion del boton Jugar.
    void* m_runningProc = nullptr;   // HANDLE del juego lanzado (nullptr = ninguno)
    f64   m_playAnimTime = 0.0;      // reloj de la animacion de "ejecutando"
    bool  m_reviewsHover = false, m_reviewsDown = false;
    ScrollPanel m_reviewsPanel;
    i32   m_reviewsFrom = -1;   // seccion desde la que se entro a resenas

    void drawGameDetail(Engine& engine, Framebuffer& fb, Recti area);
    /// Tienda de mods, a pantalla completa. Vacia por ahora.
    void drawModsSection(Engine& engine, Framebuffer& fb);
    void updateModsSection(Engine& engine, f64 dt);
    /// Carga y guarda la lista de juegos desinstalados.
    void ensureUninstalledLoaded() const;
    void saveUninstalled() const;
    /// Marca o desmarca un juego como desinstalado.
    void setUninstalled(const std::string& game, bool on);
    /// Dialogo de confirmacion antes de borrar un juego del equipo.
    void layoutUninstallAsk(Recti area);
    void drawUninstallAsk(Framebuffer& fb, Recti area);
    /// Procesa sus tres botones. Devuelve true si consumio el raton.
    bool updateUninstallAsk(Engine& engine, f64 dt);
    /// Borra de verdad: archivos, entrada de biblioteca e historial.
    void doUninstall(const std::string& game, bool copiaNube);
    void drawReviews(Engine& engine, Framebuffer& fb);

    // --- franja derecha de la Biblioteca: precio e historial de versiones ---
    /// Dibuja las dos secciones sobre la franja roja.
    void drawLibrarySidePanels(Framebuffer& fb, Recti area);
    /// Linea de tiempo de como ha subido o bajado el precio.
    void drawPriceTimeline(Framebuffer& fb, Recti area);
    /// Lista de versiones, de la mas vieja a la jugable.
    void drawVersionHistory(Framebuffer& fb, Recti area);
    /// Carga el historial del juego seleccionado si aun no se ha hecho.
    void ensureHistoryLoaded();
    /// Barra de progreso de instalacion, visible en cualquier seccion.
    void drawInstallBar(Framebuffer& fb);

    // --- seccion Modo Desarrollador ---
    /// Coloca los tres botones de la columna izquierda y el panel de detalle.
    void layoutDev(Engine& engine, i32 fbW, i32 fbH);
    void drawDev(Engine& engine, Framebuffer& fb);
    void updateDev(Engine& engine, f64 dt);
    /// Mini-seccion cuadrada que abre cada boton, a la derecha de la columna.
    /// LudoUpdates: el tablon de la zona central del Modo Desarrollador.
    void layoutLudoUpdates(Recti area);
    void drawLudoUpdates(Framebuffer& fb);
    void updateLudoUpdates(Engine& engine, f64 dt);

    void drawDevUpload(Framebuffer& fb, Recti area);
    /// Las tres opciones bajo el carril de EDAD: a mano, tramos y selector AI.
    void layoutAgeOptions(Recti panel);
    void drawAgeOptions(Framebuffer& fb, Recti panel);
    void updateAgeOptions(Engine& engine, f64 dt);
    /// ACTUALIZAR JUEGO: las tres delegan en las de GUARDADO DE ARCHIVOS, que
    /// es el contenido que la seccion tiene ahora.
    void drawDevUpdate(Framebuffer& fb, Recti area);
    void layoutDevUpdate(Recti area);
    void updateDevUpdate(Engine& engine, f64 dt);
    /// Abre una mini-seccion del Modo Desarrollador (kDevNone la cierra) y
    /// deja listo lo que esa seccion necesita: el deslizador en el precio
    /// guardado, el formulario con la ficha ya publicada.
    ///
    /// Existe para que abrirla a mano y abrirla sola -- como hace el guardado
    /// al terminar -- den exactamente el mismo estado.
    void openDevPanel(i32 panel);
    /// Juego con archivos guardados pero sin publicar ("" si no hay). Es a
    /// quien se le pone precio cuando todavia no existe en el catalogo.
    std::string firstPendingGame() const;
    /// Traduce el codigo resuelto al indicador de m_updFound.
    void lookupUpdateGame();
    /// Registra la version en el catalogo y el historial, tras empaquetarla.
    void submitUpdate();
    /// Publica la version recien guardada: catalogo (y con el la tienda),
    /// linea de tiempo y la biblioteca de quien ya tenga el juego. La llama
    /// `pollArchive` sola, en cuanto el paquete queda bien sellado.
    void publishSavedVersion(const std::string& nombre, bool& publicada);
    /// El cuadro de CHANGE LOG o de COMUNICADO, superpuesto a la seccion.
    void drawUpdateBox(Framebuffer& fb, Recti area);
    void layoutUpdateBox(Recti area);
    /// Elige la imagen que acompana al comunicado.
    void pickNoticeImage(Engine& engine);
    /// Publica el comunicado que se esta redactando.
    void submitNotice();

    /// NOTIFICACIONES: los comunicados del creador del juego seleccionado.
    void drawNoticesSection(Engine& engine, Framebuffer& fb);
    void updateNoticesSection(Engine& engine, f64 dt);
    /// Relee los comunicados del juego seleccionado si hace falta.
    void ensureNoticesLoaded();
    void drawDevPrice(Framebuffer& fb, Recti area);
    /// Mini-seccion CREAR COMUNIDAD: rejilla de nueve ajustes y, encima, el
    /// panel del que este abierto.
    void drawDevCommunity(Framebuffer& fb, Recti area);
    void layoutDevCommunity(Recti area);
    void updateDevCommunity(Engine& engine, f64 dt);
    /// Panel del ajuste abierto, superpuesto a la rejilla.
    void drawCommunityPanel(Framebuffer& fb);
    /// Texto que resume cada ajuste dentro de su boton ("" si esta sin tocar).
    std::string communitySlotValue(i32 slot) const;
    /// Mini-seccion ELIMINAR JUEGO: carmesi sobre negro.
    void drawDevDelete(Framebuffer& fb, Recti area);
    void layoutDevDelete(Recti area);
    void updateDevDelete(Engine& engine, f64 dt);
    /// Busca el nombre escrito en el catalogo propio.
    void lookupDeleteGame();
    /// Quita el juego de la tienda y LIBERA sus archivos a quien lo tenga.
    void confirmDeleteGame();
    /// Mini-seccion GUARDAR ARCHIVOS: mete el juego entero en la plataforma.
    void drawDevFiles(Framebuffer& fb, Recti area);
    void layoutDevFiles(Recti area);
    void updateDevFiles(Engine& engine, f64 dt);
    /// Abre el dialogo de carpeta y mide lo que se va a guardar.
    void pickArchiveFolder(Engine& engine);
    /// Arranca el sellado en segundo plano.
    void startArchive();
    /// Recoge el resultado del hilo y lo anota en el catalogo.
    void pollArchive();
    /// Resuelve a que juego pertenece el codigo escrito.
    void lookupArchiveCode();
    /// Genera un codigo nuevo de 15 caracteres y lo deja en el campo.
    void generateGameCode();
    /// Rejilla de seis ajustes de publicacion (moneda, pais, idiomas...).
    void layoutPriceOptions(Recti area);
    void drawPriceOptions(Framebuffer& fb);
    void updatePriceOptions(Engine& engine);
    /// Rombo giratorio arcoiris "subir codigo nuevo", junto a ACTUALIZAR JUEGO.

    // --- formulario de SUBIR JUEGO -----------------------------------------
    /// Coloca los diez botones, la zona de subida central y el desplegable
    /// abierto (si hay). Se llama desde layoutDev y antes de dibujar.
    void layoutUpload(Recti area);
    void updateUpload(Engine& engine, f64 dt);
    /// Panel desplegado del campo abierto (nombre, descripcion, generos...).
    void drawUploadPanel(Framebuffer& fb);
    /// Palomita verde que aparece "de la nada" al aceptar el precio.
    /// `t` va de 0 a 1: el circulo se abre, el trazo se dibuja solo y el
    /// conjunto se desvanece al final.
    void drawCheckmark(Framebuffer& fb, i32 cx, i32 cy, i32 radius, f32 t) const;

    /// Simbolo + giratorio de la zona de subida: gira sobre su centro.
    void drawSpinningPlus(Framebuffer& fb, i32 cx, i32 cy, i32 r, f64 t,
                          Color c) const;
    /// Abre el dialogo de Windows y registra la CARPETA elegida. Es lo unico
    /// que la zona de subida acepta: un juego es un arbol de archivos, no un
    /// archivo suelto.
    // pickGameFolder() se retiro con la zona SUBIR ARCHIVOS del formulario:
    // los archivos se suben en GUARDAR ARCHIVOS Y ACTUALIZAR, con
    // pickArchiveFolder().
    /// Elige la imagen promocional del campo 8 y la carga para previsualizar.
    void pickPromoArt(Engine& engine);
    /// Dibuja un aviso de condiciones con sus dos botones. Devuelve el alto
    /// que ocupo el texto, para colocar los botones debajo.
    void drawConsentNotice(Framebuffer& fb, Recti panel, const char* const* lineas,
                           i32 nLineas, i32 consent, Color cYes, Color cNo,
                           const char* txtYes, const char* txtNo);
    /// Procesa los dos botones del aviso abierto sobre `consent`.
    void updateConsentNotice(Engine& engine, i32& consent);

    /// Reune lo escrito en el formulario en una ficha de catalogo.
    devcatalog::Listing buildListing() const;
    /// Vuelca una ficha guardada sobre los campos del formulario.
    void loadListing(const devcatalog::Listing& l);
    /// Publica (o actualiza) el juego con lo que hay ahora en el formulario.
    void submitUpload();
    /// Cierra la parte de la ficha: comprueba los diez campos y, si estan,
    /// la da por terminada y lleva a GUARDAR ARCHIVOS.
    void finishUploadEditing();

    // --- ficha del producto ---
    void layoutProduct(Engine& engine, i32 fbW, i32 fbH);
    void drawProduct(Engine& engine, Framebuffer& fb);
    void updateProduct(Engine& engine, f64 dt);
    /// Abre la ficha del juego `idx` del catalogo de la Tienda.
    void openProduct(i32 idx);
    /// Carga imagen e historial del producto abierto, si aun no lo estan.
    void ensureProductLoaded();
    /// Linea cronologica de precios dentro de la franja derecha.
    void drawProductPrices(Framebuffer& fb, Recti area);
    /// Lista de versiones dentro de la franja derecha.
    void drawProductVersions(Framebuffer& fb, Recti area);

    // --- campo 10: EULA propio, a pantalla casi completa ---
    void layoutEula(Engine& engine, i32 fbW, i32 fbH);
    void drawEula(Engine& engine, Framebuffer& fb);
    void updateEula(Engine& engine, f64 dt);

    /// Dibuja un boton con el remolino verde/violeta: el relleno se revuelve
    /// por columnas, de modo que los dos colores se arrastran por dentro en
    /// vez de latir a la vez. Lo comparten GUARDAR Y SUBIR y ACEPTAR PRECIO.
    /// `l1`/`l2` es la etiqueta partida para cuando el boton sea estrecho.
    void drawSwirlButton(Framebuffer& fb, Recti r, f64 t, bool hover, bool down,
                         const char* entero, const char* l1, const char* l2) const;

    /// Mezcla irregular de dos colores para las animaciones de los botones.
    /// `t` es el reloj y `semilla` separa la fase de cada boton, de modo que
    /// no laten todos a la vez. La irregularidad sale de sumar tres senos de
    /// periodos primos entre si: no se repite a simple vista.
    static Color mixAnimated(Color a, Color b, f64 t, f32 semilla);
    /// Reparte el teclado entre los dos campos de texto del formulario.
    void routeUploadKeys(Engine& engine);

    // Historial del juego seleccionado en la biblioteca: precio y versiones.
    // Se recarga al cambiar de juego, no cada frame.
    history::GameHistory m_history;
    bool        m_historyLoaded = false;
    std::string m_historyOf;      // juego cuyo historial esta cargado

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

    // --- Modo Desarrollador ---
    /// Mini-seccion abierta por los botones de la columna izquierda.
    enum DevPanel { kDevNone = -1, kDevUpload = 0, kDevUpdate = 1,
                    kDevPrice = 2, kDevDelete = 3, kDevCommunity = 4 };

    /// Cuantos botones tiene la columna. Es constante y no un 3 repetido por
    /// el archivo: anadir uno mas era tocar cuatro sitios distintos.
    static constexpr i32 kDevButtons = 5;

    Recti m_devBtnRects[kDevButtons]{};   // los botones en columna
    Recti m_devPanelRect{};      // mini-seccion cuadrada de la derecha
    i32   m_devHover   = -1;     // boton de la columna bajo el raton
    i32   m_devDown    = -1;
    i32   m_devPanel   = kDevNone;   // panel abierto

    // --- CREAR COMUNIDAD: nueve ajustes ---
    //
    // Los cuatro primeros son los que se pidieron: nombre, descripcion de
    // 5000 caracteres, reglas y cuantos admins caben (1..100). Los cinco
    // siguientes son mecanicas propias de una comunidad, pensadas para que
    // cada una resuelva algo que las otras no:
    //
    //   ENTRADA   -- como se entra: abierta, con solicitud, por invitacion o
    //                solo quien tenga el juego. Es la primera decision real
    //                de una comunidad y la que cambia a quien te diriges.
    //   RANGOS    -- escalera de rangos que se ganan jugando (horas) en vez
    //                de darse a dedo. Da a la comunidad una progresion propia
    //                ligada al juego, no un chat mas.
    //   CASTIGOS  -- que pasa al romper una regla, por tramos: aviso, silencio
    //                temporal, expulsion. Escribir reglas sin consecuencias
    //                las deja en decoracion.
    //   EVENTOS   -- plantillas de eventos recurrentes (torneo, partida
    //                comunitaria, concurso de mods) con su periodicidad.
    //   IDENTIDAD -- color e insignia de la comunidad, lo que la hace
    //                reconocible al lado de las demas.
    enum ComSlot {
        // El orden ES el de la pantalla, y cae en los tres grupos de 3+3+3:
        //
        //   QUE ES                  -- nombre, descripcion, identidad
        //   QUIEN ENTRA Y COMO SUBE -- entrada, admins, rangos
        //   SUS NORMAS Y SU DIA     -- reglas, castigos, eventos
        //
        // Las reglas van con los castigos a proposito: una regla sin
        // consecuencia no se aplica, y verlas juntas es lo que deja claro
        // que la escalera de castigos es la que hace cumplir esas reglas.
        kComName   = 0,   // nombre de la comunidad
        kComDesc   = 1,   // descripcion, 5000 caracteres
        kComBrand  = 2,   // color e insignia
        kComJoin   = 3,   // como se entra
        kComAdmins = 4,   // cuantos admins: 1..100
        kComRanks  = 5,   // rangos por horas jugadas
        kComRules  = 6,   // reglas para quien se una
        kComPunish = 7,   // castigos por tramos
        kComEvents = 8,   // eventos recurrentes
        kComSlots  = 9
    };

    /// Los nueve ajustes van en TRES grupos con rotulo, no en una rejilla
    /// plana: asi se ve de un vistazo que describe la comunidad, que gobierna
    /// su acceso y que es su dia a dia.
    static constexpr i32 kComGroups = 3;
    i32   m_comGroupY[kComGroups]{};   // donde arranca el rotulo de cada grupo

    Recti m_comRects[kComSlots]{};    // los nueve botones
    /// Franja del fondo que dice cuanto falta por rellenar. No es un boton:
    /// no se pulsa, solo informa, igual que el aviso al pie de las otras
    /// mini-secciones.
    Recti m_comSummaryRect{};
    i32   m_comHover = -1;
    i32   m_comDown  = -1;
    /// Ajuste abierto (-1 = ninguno). Su panel se superpone a la rejilla.
    i32   m_comOpen  = -1;
    Recti m_comPanelRect{};

    TextField m_comNameField;
    TextField m_comDescField;    // 5000 caracteres
    TextField m_comRulesField;

    /// Cuantos admins admite la comunidad. Se guarda el numero y no un 0..1
    /// como en los carriles de SUBIR JUEGO: el rango es corto y entero, y
    /// redondear un flotante daria 99 donde el autor puso 100.
    i32   m_comAdmins = 5;
    Recti m_comAdminsTrack{};     // carril
    Recti m_comAdminsLess{}, m_comAdminsMore{};   // -1 y +1, para clavar la cifra
    bool  m_comAdminsDrag = false;

    /// Via de entrada elegida, indice en kComJoinModes.
    i32   m_comJoinMode = 0;
    /// Rangos: cuantos escalones y cada cuantas horas sube uno.
    i32   m_comRankCount = 3;
    i32   m_comRankHours = 10;
    /// Castigos: los tres tramos, en indices de kComPunishSteps.
    i32   m_comPunish[3] = { 0, 1, 2 };
    i32   m_comPunishOpen = -1;   // tramo desplegado
    /// Eventos marcados, por indice en kComEventKinds.
    std::vector<i32> m_comEvents;
    /// Identidad: color e insignia, por indice en sus tablas.
    i32   m_comColor  = 0;
    i32   m_comBadge  = 0;

    /// Rectangulos de los controles del panel abierto. Se rellenan al dibujar
    /// y los lee el raton en el frame siguiente, como en SUBIR JUEGO.
    Recti m_comOptRects[12]{};
    i32   m_comOptCount = 0;
    i32   m_comOptHover = -1;
    /// Reloj de las animaciones del Modo Desarrollador. Lo usa el remolino
    /// del boton ACEPTAR PRECIO; corre mientras la seccion este visible.
    f64   m_devAnimClock = 0.0;

    // --- LudoUpdates: el tablon del Modo Desarrollador ---
    //
    // Ocupa la zona central cuando no hay ninguna mini-seccion abierta, que
    // hasta ahora era negro vacio. Son diez bloques anchos con scroll donde
    // iran las reglas, los cambios del sistema y los avisos a creadores.
    //
    // El TEXTO es provisional a proposito: el encargo es el continente, no
    // el contenido. Cada bloque trae su titulo definitivo y un cuerpo de
    // relleno que se sustituye sin tocar la maquetacion.
    static constexpr i32 kLudoBlocks = 8;

    ScrollPanel m_ludoPanel;        // el scroll del tablon
    Recti m_ludoRect{};             // la zona entera (titulo + lista)
    Recti m_ludoBlockRect[kLudoBlocks]{};   // cada bloque, ya desplazado
    /// Reloj del degradado del titulo. Corre con la seccion visible.
    f64   m_ludoClock = 0.0;

    // Deslizador de precio: 0..1, donde 0 = Gratis y 1 = el maximo.
    Recti m_priceTrackRect{};    // barrita
    Recti m_priceKnobRect{};     // selector rectangular que se arrastra
    /// Posicion normalizada del selector: 0 = extremo izquierdo (el precio
    /// maximo), 1 = extremo derecho (GRATIS). Arranca en 1 porque sin tocar
    /// nada el juego no tiene precio puesto.
    f32   m_priceT       = 0.0f;   // 0 = GRATIS = sin tocar
    bool  m_priceDragging = false;
    bool  m_priceKnobHover = false;

    enum PriceOption {
        kOptCurrency = 0,   // moneda en que se cobra
        kOptCountry  = 1,   // pais que sale primero
        kOptLanguage = 2,   // idiomas a los que se traduce
        kOptRegion   = 3,   // region de lanzamiento
        kOptTax      = 4,   // impuestos incluidos o aparte
        kOptRounding = 5,   // redondeo del precio convertido
    };

    Recti m_priceOptRects[kPriceOptCount]{};
    i32   m_priceOptHover = -1;
    i32   m_priceOptDown  = -1;
    /// Boton cuya lista de opciones esta desplegada (-1 = ninguna).
    i32   m_priceOptOpen  = -1;
    /// Indice de la eleccion actual de cada boton.
    i32   m_priceOptSel[kPriceOptCount]{};
    /// true si el usuario ELIGIO ese ajuste. Sin esto no se distingue "no lo
    /// ha tocado" de "eligio la primera opcion", que es justo lo que hay que
    /// exigir antes de aceptar el precio.
    bool  m_priceOptSet[kPriceOptCount]{};
    /// Rectangulos de la lista desplegada, para poder pulsar en ella.
    Recti m_priceOptListRect{};
    i32   m_priceOptListHover = -1;

    // --- formulario de SUBIR JUEGO ---
    /// Los diez campos. Los cinco primeros tienen contenido; del 6 al 10
    /// quedan reservados, dibujados pero inertes.
    enum UploadSlot {
        kUpNone   = -1,
        kUpName   = 0,   // escribir nombre del juego
        kUpDesc   = 1,   // descripcion (1000 caracteres)
        kUpGenre  = 2,   // elegir genero (#etiquetas)
        kUpPerf   = 3,   // elegir rendimiento (fps de fabrica)
        kUpAge    = 4,   // edad recomendada
        kUpMulti  = 5,   // tipo de multijugador (@etiquetas)
        kUpPirate = 6,   // permiso de pirateria (aviso + aceptar/rechazar)
        kUpArt    = 7,   // diseno de seccion (imagen promocional)
        kUpCustom = 8,   // permitir personalizacion (aviso + aceptar/rechazar)
        kUpEula   = 9,   // EULA propio: tres bloques de reglas
    };

    /// Los tres bloques del EULA. Cada uno admite texto sin limite practico y
    /// cubre un ambito distinto de las reglas del juego.
    enum EulaBlock {
        kEulaLocal = 0,   // Local / Un jugador
        kEulaMulti = 1,   // Multijugador / Servidores
        kEulaMods  = 2,   // Comunidad y moding
        kEulaBlocks = 3,
    };

    /// Respuesta a un aviso de condiciones: sin contestar, aceptado o
    /// rechazado. Se guarda aparte del boton para que la eleccion sobreviva a
    /// cerrar y reabrir el panel.
    enum Consent { kConsentNone = 0, kConsentYes = 1, kConsentNo = 2 };

    Recti m_upRects[kUploadSlots]{};   // los diez botones
    Recti m_upDropRect{};              // pilar central (decorativo, ver layout)

    // --- boton TERMINE DE EDITAR, bajo el EULA ---
    //
    // Cierra la parte de la FICHA. Comprueba que los diez campos estan puestos
    // y, si es asi, lleva a GUARDAR ARCHIVOS, que es el paso siguiente. No
    // publica nada: publicar sigue necesitando archivos y precio.
    Recti m_upDoneRect{};
    bool  m_upDoneHover = false, m_upDoneDown = false;

    /// true cuando la ficha se dio por terminada con el boton.
    ///
    /// Se guarda aparte de "estan los diez campos" a proposito: el autor puede
    /// tener todo escrito y seguir retocando, y PUBLICAR no debe encenderse a
    /// mitad de una frase. Se enciende al pulsar el boton, y se apaga solo si
    /// despues se vacia algun campo.
    bool  m_upFichaLista = false;
    Recti m_upPanelRect{};             // panel del campo abierto
    i32   m_upHover = kUpNone;
    i32   m_upDown  = kUpNone;
    i32   m_upOpen  = kUpNone;         // campo desplegado (-1 = ninguno)
    bool  m_upDropHover = false;       // cursor sobre la zona de subida
    bool  m_upDropDown  = false;
    f64   m_upPlusTime  = 0.0;         // reloj del giro del simbolo +

    TextField m_upNameField;           // campo 1
    TextField m_upDescField;           // campo 2
    TextField m_upGenreField;          // campo 3: siempre empieza por '#'

    /// Generos marcados, por indice en la tabla kUpGenres.
    std::vector<i32> m_upGenres;

    /// Deslizadores TOCADOS alguna vez.
    ///
    /// RENDIMIENTO y EDAD siempre tienen un valor, asi que no se puede saber
    /// si estan "puestos" mirando su contenido como en los campos de texto.
    /// Lo que distingue un valor ELEGIDO de uno por defecto es que el autor
    /// haya pasado por ahi, y eso es lo que se anota.
    bool m_upPerfTocado = false;
    bool m_upAgeTocado  = false;

    f32  m_upFpsT = 0.0f;              // 0..1 sobre [kUpFpsMin, kUpFpsMax]
    /// Extremos de la franja de edad, los dos sobre [kUpAgeMin, kUpAgeMax].
    /// `m_upAgeT` es la MINIMA y conserva su nombre porque lo usan la ficha,
    /// las tres vias de fijarla y varias pruebas.
    f32  m_upAgeT    = 0.0f;           // edad minima
    f32  m_upAgeMaxT = 1.0f;           // edad maxima
    bool m_upFpsDrag = false;
    bool m_upAgeDrag = false;          // arrastrando la minima
    bool m_upAgeMaxDrag = false;       // arrastrando la maxima
    Recti m_upTrackRect{};             // carril del deslizador abierto
    Recti m_upKnobRect{};              // su selector
    /// Segundo carril: solo existe en EDAD, para la maxima.
    Recti m_upTrack2Rect{};
    Recti m_upKnob2Rect{};

    // --- EDAD RECOMENDADA: tres formas de fijarla, bajo el carril ---
    //
    // El deslizador da UNA edad minima, que es lo que guarda la ficha. Las
    // tres opciones de abajo son maneras distintas de llegar a ese numero:
    //
    //   1. A MANO       -- se escribe la cifra. Es la via exacta.
    //   2. TRAMOS       -- ocho pares de edades habituales (20-35, 13-17...).
    //                      Al elegir uno se fija la edad MENOR del par, que
    //                      es la que tiene sentido como minima recomendada.
    //   3. SELECTOR AI  -- opcional, propone un tramo de entre nueve.
    //
    // Las tres escriben en m_upAgeT, asi que el resto del formulario (la
    // ficha, el resumen del boton, el guardado) no se entera de cual se uso.
    enum AgeMode { kAgeNone = -1, kAgeManual = 0, kAgeRange = 1, kAgeAi = 2 };

    static constexpr i32 kAgeOpts   = 3;   // los tres botones de modo
    static constexpr i32 kAgeRanges = 8;   // tramos seleccionables
    static constexpr i32 kAgeAiOpts = 9;   // propuestas del selector AI

    Recti m_ageOptRect[kAgeOpts]{};        // los tres botones
    i32   m_ageOptHover = kAgeNone;
    i32   m_ageOptDown  = kAgeNone;
    i32   m_ageMode     = kAgeNone;        // cual esta desplegado

    TextField m_ageManualField;            // la cifra escrita a mano
    Recti m_ageManualRect{};               // su caja
    Recti m_ageManualOkRect{};             // confirmar
    bool  m_ageManualOkHover = false, m_ageManualOkDown = false;
    std::string m_ageMsg;                  // aviso al pie
    bool  m_ageMsgErr = false;

    Recti m_ageRangeRect[kAgeRanges]{};    // rejilla de tramos
    i32   m_ageRangeHover = -1;
    i32   m_ageRangeSel   = -1;            // tramo elegido (-1 ninguno)

    Recti m_ageAiRect{};                   // boton "lanzar propuesta"
    bool  m_ageAiHover = false, m_ageAiDown = false;
    i32   m_ageAiPick  = -1;               // propuesta mostrada (-1 ninguna)
    /// Animacion del sorteo: va de 1 a 0 mientras las opciones parpadean.
    f64   m_ageAiSpin  = 0.0;
    /// Semilla del sorteo. No se usa rand() global: con una semilla propia
    /// el sorteo es reproducible en las pruebas.
    u32   m_ageAiSeed  = 0x9E3779B9u;

    ScrollPanel m_upGenrePanel;        // lista de generos con scroll
    std::wstring m_upCodePath;         // carpeta elegida en la zona de subida

    // --- campo 6: tipo de multijugador ---
    TextField m_upMultiField;          // siempre empieza por '@'
    std::vector<i32> m_upMulti;        // modos marcados, por indice
    ScrollPanel m_upMultiPanel;        // lista de modos con scroll

    // --- campos 7 y 9: avisos con aceptar/rechazar ---
    // Se guarda la respuesta, no solo el boton pulsado, para que la eleccion
    // siga ahi al cerrar y reabrir el panel.
    i32   m_upPirateConsent = kConsentNone;
    i32   m_upCustomConsent = kConsentNone;
    Recti m_upYesRect{};               // boton de aceptar del aviso abierto
    Recti m_upNoRect{};                // boton de rechazar
    bool  m_upYesHover = false, m_upYesDown = false;
    bool  m_upNoHover  = false, m_upNoDown  = false;
    ScrollPanel m_upNoticePanel;       // texto del aviso, con scroll

    // --- campo 8: diseno de seccion (imagen promocional) ---
    std::wstring     m_upArtPath;      // imagen elegida
    std::vector<u32> m_upArtPixels;    // ya decodificada, para previsualizar
    i32   m_upArtW = 0, m_upArtH = 0;
    Recti m_upArtDropRect{};           // rectangulo largo donde se suelta
    bool  m_upArtHover = false, m_upArtDown = false;
    std::string m_upArtMsg;            // resultado de la ultima carga

    // --- campo 10: EULA propio ---
    // Se abre a pantalla casi completa: son tres bloques de texto largo, y en
    // la mini-seccion cuadrada no se leerian.
    TextField   m_eulaField[kEulaBlocks];
    ScrollPanel m_eulaPanel[kEulaBlocks];
    Recti       m_eulaRects[kEulaBlocks]{};   // las tres cajas
    i32         m_eulaFocus = kEulaLocal;     // bloque que recibe el teclado
    Recti       m_eulaCloseRect{};            // boton de volver
    bool        m_eulaCloseHover = false;
    f64         m_eulaAnimTime = 0.0;         // reloj de la mezcla irregular

    // --- GUARDAR Y SUBIR: RETIRADO ---
    //
    // Era un boton al final de la columna derecha que llamaba exactamente a
    // la misma funcion que PUBLICAR (submitUpload). Dos botones para una sola
    // accion, y el de arriba ya encabeza el formulario junto al nombre.
    //
    // El rectangulo se conserva SIEMPRE VACIO en vez de borrar el miembro:
    // `uploadSaveRect()` es parte de la API de pruebas y el aviso de error se
    // coloca mirando si vale algo. Con w = 0 nada se dibuja ni responde.
    Recti m_upSaveRect{};
    bool  m_upSaveHover = false, m_upSaveDown = false;

    /// Aviso bajo el boton de subir. En segundos que quedan; 0 = sin aviso.
    /// Sirve para los dos casos, que comparten franja y temporizador:
    ///   - falta el precio -> rojo, y el boton tiembla,
    ///   - subido a la tienda -> verde, sin temblor.
    f64   m_upPriceWarn = 0.0;
    /// true si el aviso en curso es de exito (verde) en vez de error (rojo).
    bool  m_upWarnOk = false;
    /// Que dice el aviso rojo. Los dos errores comparten franja y temblor,
    /// solo cambia el texto.
    enum UpWarn {
        kWarnPrice = 0,   // falta decidir el precio
        kWarnCode  = 1,   // falta la carpeta del juego
        kWarnFiles = 2,   // faltan los archivos guardados
        kWarnCampo = 3,   // falta un campo de la ficha (dice cual)
        kWarnFicha = 4,   // la ficha esta llena pero sin cerrar con el boton
    };
    i32   m_upWarnKind = kWarnPrice;
    /// Campo que falta cuando el aviso es kWarnCampo. Es un UploadSlot.
    i32   m_upWarnSlot = kUpName;

    // --- ACEPTAR PRECIO: bajo la barrita de la mini-seccion PRECIOS ---
    // Mismo aspecto que GUARDAR Y SUBIR (remolino verde/violeta): las dos son
    // la accion que cierra su seccion, y compartir aspecto lo dice sin texto.
    Recti m_priceOkRect{};
    bool  m_priceOkHover = false, m_priceOkDown = false;

    /// Aviso "elige moneda y paises": tiembla igual que el de GUARDAR Y
    /// SUBIR. Segundos que le quedan; 0 = sin aviso.
    f64   m_priceOptWarn = 0.0;

    // --- ACTUALIZAR JUEGO ---
    /// La seccion se vacio: su contenido es ahora el de GUARDADO DE ARCHIVOS,
    /// cuyo estado vive en los campos `m_arc*`. De lo que tenia (cuatro pasos,
    /// zona de subida, change log, aviso de compatibilidad) solo queda esto.
    ///
    /// Resultado de resolver el codigo: -1 sin escribir, 0 no cuadra con
    /// ningun juego publicado, 1 cuadra.
    i32   m_updFound = -1;

    // --- ELIMINAR JUEGO ---
    /// Se escribe el CODIGO, se lee el aviso y se confirma. Borrar de la
    /// tienda no borra de las bibliotecas: los archivos se LIBERAN, porque el
    /// juego ya es de quien lo compro.
    ///
    /// Se identifica por codigo y no por nombre porque retirar un juego no se
    /// puede deshacer: el codigo es unico, no se teclea de memoria y no se
    /// confunde con el de otro juego parecido.
    TextField m_delCodeField;
    Recti m_delFieldRect{};
    /// Resultado de resolver el codigo: -1 sin escribir, 0 ningun juego tuyo
    /// lo tiene, 1 resuelto.
    i32   m_delFound = -1;
    /// Nombre del juego al que apunta el codigo. Es con lo que el catalogo y
    /// las bibliotecas indexan, asi que el borrado trabaja con el.
    std::string m_delJuego;
    Recti m_delYesRect{}, m_delNoRect{};
    bool  m_delYesHover = false, m_delNoHover = false;
    bool  m_delYesDown  = false, m_delNoDown  = false;
    ScrollPanel m_delNoticePanel;   // el aviso, con scroll
    /// Aviso al pie de la seccion tras eliminar.
    std::string m_delMsg;
    bool  m_delMsgErr = false;

    // --- GUARDAR ARCHIVOS ---
    //
    // La seccion se redujo a lo esencial: UN campo y UN boton.
    //
    //   - El campo pide el CODIGO del juego, y es obligatorio. Es lo que
    //     identifica que se esta actualizando; el nombre se deduce de el.
    //   - GENERAR CODIGO crea uno nuevo de 15 caracteres. Se apaga en cuanto
    //     hay algun juego publicado: a partir de ahi el autor ya tiene su
    //     codigo y generar otro solo crearia confusion sobre cual usar.
    //
    // Lo que habia antes -- campo de nombre, cuatro botones de atajo, la
    // seccion JUEGOS -- se retiro: cada cosa se edita donde le corresponde.
    TextField m_arcCodeField;        // el codigo, obligatorio
    Recti m_arcCodeRect{};
    /// Ultimo codigo resuelto, para no rebuscar en el catalogo cada frame.
    std::string m_arcCodeLast;
    /// Nombre del juego al que corresponde el codigo ("" si no cuadra).
    std::string m_arcCodeJuego;

    Recti m_arcGenRect{};            // GENERAR CODIGO
    bool  m_arcGenHover = false, m_arcGenDown = false;
    /// Codigo recien generado, para enseñarlo y poder copiarlo.
    std::string m_arcGenerado;

    Recti m_arcPickRect{};           // elegir carpeta
    Recti m_arcSaveRect{};           // guardar de verdad
    bool  m_arcPickHover = false, m_arcPickDown = false;
    bool  m_arcSaveHover = false, m_arcSaveDown = false;
    std::wstring m_arcFolder;        // carpeta elegida ("" si ninguna)
    i32   m_arcFiles = 0;            // archivos contados en esa carpeta
    u64   m_arcBytes = 0;            // y lo que ocupan
    GameArchive m_archive;           // el sellado, en su hilo
    bool  m_arcStarted = false;      // hay un guardado en curso por recoger
    std::string m_arcMsg;            // aviso al pie
    bool  m_arcMsgErr = false;
    /// Animacion del tic verde al terminar: 0 = apagado, baja hasta 0.
    f64   m_arcDoneAnim = 0.0;

    // --- los tres botones de ACTUALIZAR ---
    //
    // EDITAR EULA y SUBIR CHANGE LOG van en fila; DAR COMUNICADO debajo, mas
    // ancho, porque no es parte de publicar una version: es un mensaje del
    // autor a quien ya tiene el juego, y se usa en otro momento.
    Recti m_updEulaRect{};           // EDITAR EULA -> seccion del EULA
    bool  m_updEulaHover = false, m_updEulaDown = false;
    Recti m_updLogRect{};            // SUBIR CHANGE LOG -> abre su cuadro
    bool  m_updLogHover = false, m_updLogDown = false;
    Recti m_updNoticeRect{};         // DAR COMUNICADO -> abre su cuadro
    bool  m_updNoticeHover = false, m_updNoticeDown = false;

    /// Cuadro abierto sobre la seccion: -1 ninguno, 0 change log, 1 comunicado.
    i32   m_updBoxOpen = -1;
    static constexpr i32 kUpdBoxLog    = 0;
    static constexpr i32 kUpdBoxNotice = 1;
    Recti m_updBoxRect{};            // el cuadro entero
    Recti m_updBoxOkRect{};          // su boton de confirmar
    bool  m_updBoxOkHover = false, m_updBoxOkDown = false;

    /// Notas de la version. Se escriben aqui y las consume la publicacion
    /// automatica: es lo que leen los jugadores en la tienda.
    TextField m_updLogField;
    Recti m_updLogFieldRect{};

    // --- comunicado que se esta redactando ---
    /// Titulo y cuerpo. El cuerpo NO tiene limite de longitud: se pidio que
    /// pudiera tener "mucho o poco texto sin limite".
    TextField m_updNoticeTitle;
    TextField m_updNoticeBody;
    Recti m_updNoticeTitleRect{};
    Recti m_updNoticeBodyRect{};
    /// Imagen adjunta al comunicado ("" si no lleva ninguna).
    Recti m_updNoticeImgRect{};
    bool  m_updNoticeImgHover = false, m_updNoticeImgDown = false;
    std::wstring m_updNoticeImg;
    std::vector<u32> m_updNoticeImgPix;   // decodificada, para la vista previa
    i32   m_updNoticeImgW = 0, m_updNoticeImgH = 0;
    /// Cual de los dos campos del comunicado tiene el cursor (0 titulo, 1 cuerpo).
    i32   m_updNoticeFocus = 0;

    // --- PRECIO A ESCRITO ---
    /// Alternativa al deslizador: se escribe la cifra a mano. Existe porque
    /// con el carril es imposible clavar un precio exacto -- 19.99 esta a un
    /// pixel de 20.04 --, y el precio es justo el dato que hay que poner
    /// exacto.
    Recti m_priceTypeRect{};        // boton que abre el cuadro
    bool  m_priceTypeHover = false, m_priceTypeDown = false;
    bool  m_priceTypeOpen  = false;
    TextField m_priceTypeField;     // la cifra, en dolares
    Recti m_priceTypeFieldRect{};
    Recti m_priceTypeOkRect{};      // confirmar
    bool  m_priceTypeOkHover = false, m_priceTypeOkDown = false;
    std::string m_priceTypeMsg;     // aviso si la cifra no vale
    bool  m_priceTypeMsgErr = false;

    // --- MANTENER PRECIOS POR DEFECTO ---
    /// Da por buenos los seis ajustes tal y como estan, sin tener que abrir
    /// cada lista. Al pulsarlo aparecen dos mini-botones de aceptar y negar.
    Recti m_priceDefRect{};
    bool  m_priceDefHover = false, m_priceDefDown = false;
    bool  m_priceDefOpen  = false;   // mini-botones desplegados
    Recti m_priceDefYes{}, m_priceDefNo{};
    bool  m_priceDefYesHover = false, m_priceDefNoHover = false;

    /// Palomita verde de precio aceptado. Cuenta HACIA ARRIBA desde 0: la
    /// animacion se dibuja en funcion de lo avanzada que este, y al llegar al
    /// final se salta a SUBIR JUEGO.
    f64   m_priceOkAnim = 0.0;
    bool  m_priceOkAnimOn = false;

    /// true cuando el precio ya se acepto con el boton ACEPTAR PRECIO. Es lo
    /// que mira SUBIR JUEGO: con el precio dado por bueno deja de exigirlo, y
    /// el aviso pasa a ser la confirmacion de que el juego subio.
    bool  m_priceAccepted = false;

    // --- ficha del producto (seccion kProductSection) ---
    /// Las dos vistas de la franja derecha: como ha variado el precio y que
    /// versiones ha tenido. Comparten sitio y se alternan con dos botones.
    enum ProductTab { kTabPrices = 0, kTabVersions = 1 };

    Recti       m_prodArtRect{};        // imagen que puso el creador
    Recti       m_prodSideRect{};       // franja derecha, 800 px de alto
    Recti       m_prodTabRect[2]{};     // botones PRECIOS / VERSIONES
    i32         m_prodTab      = kTabPrices;
    i32         m_prodTabHover = -1;
    Recti       m_prodBackRect{};       // volver a la Tienda
    bool        m_prodBackHover = false;

    /// DESCARGAR: dentro de la imagen, pegado a su pared derecha y abajo del
    /// todo. Es la accion principal de la ficha, de ahi que vaya sobre la
    /// propia imagen y no en un lateral.
    Recti m_prodBuyRect{};
    bool  m_prodBuyHover = false, m_prodBuyDown = false;
    /// Aviso bajo el boton tras pulsarlo. Segundos que quedan; 0 = sin aviso.
    f64   m_prodBuyMsgTime = 0.0;
    std::string m_prodBuyMsg;
    bool  m_prodBuyMsgErr = false;

    /// Resultado de la revision de seguridad de la ultima descarga.
    ///
    /// Vive aqui, y no en una variable local, porque el aviso de la descarga
    /// se arma en un sitio y se redacta en otro: el juego se revisa antes de
    /// registrarlo en la biblioteca, y el mensaje final se compone cuando ya
    /// esta registrado.
    std::string m_prodScanMsg;
    bool        m_prodScanAviso = false;
    ScrollPanel m_prodReviewsPanel;     // resenas, abajo y con scroll
    ScrollPanel m_prodSidePanel;        // la franja derecha tambien desplaza

    /// Imagen promocional del producto abierto, cargada una vez.
    std::vector<u32> m_prodArtPixels;
    i32   m_prodArtW = 0, m_prodArtH = 0;
    std::string m_prodArtOf;            // juego cuya imagen esta cargada

    /// Historial del producto abierto: precios y versiones.
    history::GameHistory m_prodHistory;
    std::string m_prodHistoryOf;

    // --- publicar ---
    Recti m_upPublishRect{};           // boton al pie del formulario
    bool  m_upPublishHover = false, m_upPublishDown = false;
    /// Juego cuya ficha esta cargada en el formulario ("" = ninguno todavia,
    /// asi que PUBLICAR crea uno nuevo en vez de actualizar).
    std::string m_upEditing;

    /// Catalogo de juegos publicados por este usuario (Modo Desarrollador).
    std::vector<devcatalog::Game> m_devGames;
    bool        m_devGamesLoaded = false;
    std::string m_devMsg;          // aviso al pie de la mini-seccion
    bool        m_devMsgErr = false;

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
