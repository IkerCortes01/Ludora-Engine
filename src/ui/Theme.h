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

} // namespace theme
} // namespace ludora
