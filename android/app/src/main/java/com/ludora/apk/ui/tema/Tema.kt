package com.ludora.apk.ui.tema

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * La paleta de Ludora, la MISMA del escritorio.
 *
 * ---------------------------------------------------------------------------
 * DE DONDE SALEN LOS COLORES
 * ---------------------------------------------------------------------------
 * Uno a uno de `src/ui/Theme.h` del Ludora de PC, con el mismo nombre (sin la
 * `k`). "Negro dominante, rojo como unico acento", la franja azul del menu, el
 * violeta de los paneles, el vino del chat y los azules de la tienda.
 *
 * Antes este archivo tenia una paleta propia de Material (claro y oscuro) y el
 * telefono parecia otro producto. Ahora no hay esquema claro: el escritorio no
 * lo tiene, y la app se ve igual que alli a cualquier hora.
 *
 * Si un color cambia en Theme.h, se cambia aqui el del mismo nombre.
 */
object Ludora {
    // Superficies
    val Bg = Color(0xFF090909)            // lienzo
    val BarBg = Color(0xFF121212)         // barra de titulo
    val FieldBg = Color(0xFF141414)
    val FieldBgFocus = Color(0xFF1A1010)

    // Acento
    val Accent = Color(0xFFE01E23)        // rojo de marca
    val Border = Color(0xFF8E1116)        // marco y separador
    val FieldBorder = Color(0xFF3A0E10)
    val LedRed = Color(0xFFFF0018)        // el bloque activo del menu
    val DarkBlue = Color(0xFF0B1A4A)      // la franja del menu
    val BlueDivider = Color(0xFF040A1F)   // los bloques en reposo

    // Paneles: violeta profundo mezclado con azul oscuro
    val PanelViolet = Color(0xFF2B1B57)
    val PanelDeep = Color(0xFF1A1440)
    val PanelEdge = Color(0xFF4A3585)
    val ScrollTrack = Color(0xFF140F30)
    val ScrollThumb = Color(0xFF6C4FC4)

    // Avatar de la esquina
    val Turquoise = Color(0xFF00E5D0)

    // Chat
    val Wine = Color(0xFF5A3540)
    val WineEdge = Color(0xFF704452)

    // Tienda
    val SteamBg = Color(0xFF1B2838)
    val SteamPanel = Color(0xFF16202D)
    val SteamEdge = Color(0xFF3D4E62)
    val SteamBlue = Color(0xFF66C0F4)
    val SteamGreen = Color(0xFFA4D007)
    val SteamText = Color(0xFFC7D5E0)
    val SteamDim = Color(0xFF8F98A0)

    // Realces
    val Hover = Color(0xFF3A0E10)
    val Pressed = Color(0xFF5A1519)

    // Texto
    val Text = Color(0xFFF2F2F2)
    val Glyph = Color(0xFFB8B8B8)
    val Dim = Color(0xFF6E6E6E)
    val White = Color(0xFFFFFFFF)
    val Ok = Color(0xFF5FBF6A)

    // La L del logo, de la punta al pie
    val LogoCyan = Color(0xFF28C8F0)
    val LogoBlue = Color(0xFF2A5CE8)
    val LogoViolet = Color(0xFF8B2BE0)
    val LogoMagenta = Color(0xFFE02BB4)
    val LogoRed = Color(0xFFF02038)
}

/**
 * Material sigue debajo (dialogos del sistema, el interruptor), pero vestido
 * con la paleta de Ludora: lo que no se pinta a mano tampoco desentona.
 */
private val EsquemaLudora = darkColorScheme(
    primary = Ludora.Accent,
    onPrimary = Ludora.White,
    secondary = Ludora.Turquoise,
    onSecondary = Ludora.Bg,
    tertiary = Ludora.ScrollThumb,
    background = Ludora.Bg,
    onBackground = Ludora.Text,
    surface = Ludora.BarBg,
    onSurface = Ludora.Text,
    surfaceVariant = Ludora.PanelViolet,
    onSurfaceVariant = Ludora.Glyph,
    outline = Ludora.Border,
    error = Ludora.LedRed,
    onError = Ludora.White,
)

/**
 * Tipografia. El escritorio escribe con la fuente del sistema (Segoe UI), sin
 * negritas y con los rotulos en mayusculas; aqui la del sistema del telefono,
 * con los mismos pesos y la misma escala relativa.
 */
private val TipografiaLudora = Typography(
    headlineMedium = TextStyle(fontWeight = FontWeight.Normal, fontSize = 22.sp, lineHeight = 28.sp),
    titleLarge = TextStyle(fontWeight = FontWeight.Normal, fontSize = 20.sp, lineHeight = 26.sp),
    titleMedium = TextStyle(fontWeight = FontWeight.Normal, fontSize = 16.sp, lineHeight = 22.sp),
    bodyLarge = TextStyle(fontWeight = FontWeight.Normal, fontSize = 16.sp, lineHeight = 22.sp),
    bodyMedium = TextStyle(fontWeight = FontWeight.Normal, fontSize = 14.sp, lineHeight = 20.sp),
    labelLarge = TextStyle(fontWeight = FontWeight.Normal, fontSize = 13.sp, lineHeight = 18.sp),
)

@Composable
fun TemaLudora(contenido: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = EsquemaLudora,
        typography = TipografiaLudora,
        content = contenido,
    )
}
