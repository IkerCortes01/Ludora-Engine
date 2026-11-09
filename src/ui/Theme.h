#pragma once
#include "core/Types.h"

namespace ludora {

/// Paleta unica del motor: negro dominante, rojo como unico acento.
/// Cambiar el tema es tocar solo este archivo.
namespace theme {

// Superficies
constexpr Color kBg          = Color::fromHex(0x090909);   // lienzo
constexpr Color kBarBg       = Color::fromHex(0x121212);   // barra de titulo
constexpr Color kFieldBg     = Color::fromHex(0x141414);
constexpr Color kFieldBgFocus= Color::fromHex(0x1A1010);

// Acento
constexpr Color kAccent      = Color::fromHex(0xE01E23);   // rojo de marca
constexpr Color kBorder      = Color::fromHex(0x8E1116);   // marco y separador
constexpr Color kFieldBorder = Color::fromHex(0x3A0E10);
constexpr Color kLedRed      = Color::fromHex(0xFF0018);   // rojo LED saturado
constexpr Color kDarkBlue    = Color::fromHex(0x0B1A4A);   // azul oscuro

// Panel de biblioteca: violeta profundo mezclado con azul oscuro.
constexpr Color kPanelViolet = Color::fromHex(0x2B1B57);   // violeta base
constexpr Color kPanelDeep   = Color::fromHex(0x1A1440);   // fondo, mas azulado
constexpr Color kPanelEdge   = Color::fromHex(0x4A3585);   // separadores
constexpr Color kScrollTrack = Color::fromHex(0x140F30);   // canal de la barra
constexpr Color kScrollThumb = Color::fromHex(0x6C4FC4);   // pulgar
constexpr Color kScrollHover = Color::fromHex(0x8B6FE0);   // pulgar resaltado
// Divisor de la barra azul: un azul mas profundo. Usar kDarkBlue lo haria
// invisible sobre si mismo, asi que se baja el tono para que la linea se lea.
constexpr Color kBlueDivider = Color::fromHex(0x040A1F);

// Avatar de la esquina inferior derecha.
constexpr Color kTurquoise   = Color::fromHex(0x00E5D0);   // turquesa saturado
constexpr Color kDevPurple   = Color::fromHex(0x8B3FE0);   // morado del modo dev

// Chat: panel lateral en vino desaturado, elegante.
constexpr Color kWine        = Color::fromHex(0x5A3540);   // vino apagado
constexpr Color kWineEdge    = Color::fromHex(0x704452);   // borde algo mas claro

// --- Modo Desarrollador: los tres botones de la columna izquierda ---
// Cada boton tiene su propia pareja linea/relleno, deliberadamente distintas
// entre si para que se distingan de un vistazo sin leer la etiqueta.
constexpr Color kDevMint     = Color::fromHex(0x3FE0A8);   // lineas: verde menta
constexpr Color kDevTeal     = Color::fromHex(0x10505C);   // relleno: azul verdoso
constexpr Color kDevBrown    = Color::fromHex(0x8B5A2B);   // lineas: marron
constexpr Color kDevIndigo   = Color::fromHex(0x2A2A7A);   // relleno: azul indigo
constexpr Color kDevGold     = Color::fromHex(0xD4AF37);   // lineas: dorado
constexpr Color kDevLime     = Color::fromHex(0x7FE01E);   // relleno: verde lima

// Deslizador de precio: azul en Gratis, rojo en el maximo. El punto medio se
// interpola entre ambos, asi que el color mismo comunica lo caro que es.
constexpr Color kPriceFree   = Color::fromHex(0x1E6EE0);   // extremo izquierdo
constexpr Color kPriceMax    = Color::fromHex(0xE01E23);   // extremo derecho

// --- Formulario de SUBIR JUEGO: cada campo tiene su pareja de color propia,
// deliberadamente distinta, para reconocerlo sin leer la etiqueta. ---
constexpr Color kUpIndigo    = Color::fromHex(0x3F51B5);   // 1 nombre: azul indigo
constexpr Color kUpPurple    = Color::fromHex(0x7B2FBE);   // 2 descripcion: morado
constexpr Color kUpBlue      = Color::fromHex(0x1E6EE0);   // 3 generos: azul...
constexpr Color kUpPink      = Color::fromHex(0xE84FA8);   // ...entrelazado con rosa
constexpr Color kUpBrown     = Color::fromHex(0x8B5A2B);   // 4 rendimiento: carril
constexpr Color kUpMint      = Color::fromHex(0x3FE0A8);   // 5 edad: selector
// Extremos del fondo de la seccion EDAD: cielo a los 4 anos, violeta a los 90.
constexpr Color kAgeSky      = Color::fromHex(0x87CEEB);   // azul cielo
constexpr Color kAgeViolet   = Color::fromHex(0x6A1B9A);   // violeta intenso
// Zona de subida central: verde solo cuando el cursor esta encima.
constexpr Color kUpDropEdge  = Color::fromHex(0x4A3585);   // en reposo
constexpr Color kUpDropHover = Color::fromHex(0x2FD46A);   // resaltada

// Campo 6 (tipo de multijugador): rojo y azul entrelazados, para que no se
// confunda con el campo 3, que usa azul y rosa.
constexpr Color kUpMultiRed  = Color::fromHex(0xE02020);
constexpr Color kUpMultiBlue = Color::fromHex(0x2060E0);

// Respuestas a los avisos de condiciones. Cada aviso usa su pareja: el de
// pirateria va en verde vivo / rojo carmesi, y el de personalizacion en los
// mismos tonos subidos a neon, para que se distingan de un vistazo.
constexpr Color kConsentGreen = Color::fromHex(0x00C853);   // verde vivo
constexpr Color kConsentRed   = Color::fromHex(0xA4133C);   // rojo carmesi
constexpr Color kNeonGreen    = Color::fromHex(0x39FF14);   // verde neon
constexpr Color kNeonRed      = Color::fromHex(0xFF1744);   // rojo neon

// --- Tienda: paleta al estilo de las grandes tiendas de juegos ---
// Azules muy oscuros y desaturados como lienzo, con un azul claro de acento
// para los precios y lo pulsable. El violeta anterior competia con el
// contenido; estos tonos lo dejan pasar a primer plano.
constexpr Color kSteamBg      = Color::fromHex(0x1B2838);   // fondo general
constexpr Color kSteamPanel   = Color::fromHex(0x16202D);   // tarjetas
constexpr Color kSteamPanelHi = Color::fromHex(0x2A475E);   // tarjeta resaltada
constexpr Color kSteamEdge    = Color::fromHex(0x3D4E62);   // separadores
constexpr Color kSteamBlue    = Color::fromHex(0x66C0F4);   // acento y enlaces
constexpr Color kSteamGreen   = Color::fromHex(0xA4D007);   // precio y descuento
constexpr Color kSteamDeal    = Color::fromHex(0x4C6B22);   // fondo del descuento
constexpr Color kSteamText    = Color::fromHex(0xC7D5E0);   // cuerpo de texto
constexpr Color kSteamDim     = Color::fromHex(0x8F98A0);   // texto secundario
constexpr Color kSteamHeader  = Color::fromHex(0x171A21);   // barra de cabecera
constexpr Color kSteamArrow   = Color::fromHex(0x7A8B99);   // flechas del carrusel
constexpr Color kSteamArrowHi = Color::fromHex(0xC7D5E0);   // flecha resaltada
constexpr Color kFollowBlue   = Color::fromHex(0x66C0F4);   // boton "Siguiendo"

// Campo 8 (diseno de seccion): marco de la zona donde se suelta la imagen.
constexpr Color kUpArtEdge    = Color::fromHex(0xD4AF37);   // dorado
constexpr Color kUpArtHover   = Color::fromHex(0xFFD966);   // dorado claro

// Campo 10 (EULA propio): rojo y naranja, mezclados con animacion irregular.
constexpr Color kUpEulaRed    = Color::fromHex(0xE02020);
constexpr Color kUpEulaOrange = Color::fromHex(0xFF8C1A);

// Panel del EULA: azules opacos y apagados, que es lo que hace legible un
// texto largo. Nada saturado: el contenido manda sobre el fondo.
constexpr Color kEulaBg       = Color::fromHex(0x121C2B);   // fondo general
constexpr Color kEulaPanel    = Color::fromHex(0x1A2738);   // las tres cajas
constexpr Color kEulaPanelFoc = Color::fromHex(0x223347);   // la que se edita
constexpr Color kEulaEdge     = Color::fromHex(0x2E4258);   // separadores
constexpr Color kEulaEdgeFoc  = Color::fromHex(0x4E7BA8);   // borde activo
constexpr Color kEulaTitle    = Color::fromHex(0x8FB4D9);   // rotulos
constexpr Color kEulaText     = Color::fromHex(0xD6E2EF);   // cuerpo del texto

// Campo 4 (rendimiento): amarillo neon, de carril y de seccion. Sustituye al
// marron, que sobre el fondo oscuro apenas se distinguia del borde.
constexpr Color kNeonYellow   = Color::fromHex(0xF5FF2B);
constexpr Color kNeonYellowDim= Color::fromHex(0x8A9410);   // el mismo, apagado

// Boton GUARDAR Y SUBIR: verde y violeta a plena saturacion, que se revuelven
// entre si. Son opuestos suficientes para que la mezcla recorra tonos
// intermedios visibles en vez de quedarse en un gris.
constexpr Color kSaveGreen    = Color::fromHex(0x00FF66);
constexpr Color kSaveViolet   = Color::fromHex(0x9000FF);

// Realces de los botones de ventana
constexpr Color kHover       = Color::fromHex(0x3A0E10);
constexpr Color kPressed     = Color::fromHex(0x5A1519);
constexpr Color kCloseHover  = Color::fromHex(0xE01E23);
constexpr Color kClosePress  = Color::fromHex(0x9B1418);
constexpr Color kSelection   = Color::fromHex(0x5A1519);

// Texto
constexpr Color kText        = Color::fromHex(0xF2F2F2);
constexpr Color kGlyph       = Color::fromHex(0xB8B8B8);   // iconos en reposo
constexpr Color kDim         = Color::fromHex(0x6E6E6E);
constexpr Color kWhite       = Color::fromHex(0xFFFFFF);
constexpr Color kOk          = Color::fromHex(0x5FBF6A);

// --- paleta del LOGO ---
// Los cinco tonos que recorre la L de Ludora, en orden: del cian de la punta
// superior al rojo del pie, pasando por el azul, el violeta y el magenta del
// quiebre. Sirven para el degradado del titulo LudoUpdates, y para cualquier
// cosa que deba leerse como "de la marca".
constexpr Color kLogoCyan    = Color::fromHex(0x28C8F0);
constexpr Color kLogoBlue    = Color::fromHex(0x2A5CE8);
constexpr Color kLogoViolet  = Color::fromHex(0x8B2BE0);
constexpr Color kLogoMagenta = Color::fromHex(0xE02BB4);
constexpr Color kLogoRed     = Color::fromHex(0xF02038);

} // namespace theme
} // namespace ludora
