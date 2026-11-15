package com.ludora.apk.ui.tema

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * Paleta y tipografia de Ludora en el telefono.
 *
 * ---------------------------------------------------------------------------
 * DE DONDE SALEN LOS COLORES
 * ---------------------------------------------------------------------------
 * Del Ludora de escritorio: el azul de la franja de menu y el violeta de los
 * paneles de la biblioteca. Son los dos colores que identifican la app, y
 * cambiarlos en el telefono haria que pareciesen dos productos distintos.
 *
 * Lo que NO se copia es la forma de pintar. El escritorio dibuja a mano sobre
 * un framebuffer de software con rectangulos de color plano. Aqui se usa
 * Material 3, que trae sombras, estados de pulsacion y contraste ya resueltos,
 * y que es lo que un usuario de Android espera al tocar la pantalla.
 */

// --- Azul de la franja de menu ---
private val LudoraAzul = Color(0xFF1B3A8C)
private val LudoraAzulClaro = Color(0xFF4A63B8)
private val LudoraAzulOscuro = Color(0xFF0D2156)

// --- Violeta de los paneles de biblioteca ---
private val LudoraVioleta = Color(0xFF6B3FA0)
private val LudoraVioletaClaro = Color(0xFF9B6FD0)

// --- Rojo de las dos barras de la seccion de paneles ---
private val LudoraRojo = Color(0xFFC0392B)

/**
 * Esquema claro.
 *
 * Se definen los colores a mano en vez de usar el color dinamico de Android 12
 * (que tomaria el tono del fondo de pantalla del usuario): una plataforma de
 * juegos tiene identidad propia, y teñirla del fondo de pantalla la diluye.
 */
private val EsquemaClaro = lightColorScheme(
    primary = LudoraAzul,
    onPrimary = Color.White,
    primaryContainer = LudoraAzulClaro,
    onPrimaryContainer = Color.White,
    secondary = LudoraVioleta,
    onSecondary = Color.White,
    secondaryContainer = LudoraVioletaClaro,
    onSecondaryContainer = Color(0xFF1A0033),
    tertiary = LudoraRojo,
    onTertiary = Color.White,
    background = Color(0xFFF7F7FA),
    onBackground = Color(0xFF1A1A1F),
    surface = Color.White,
    onSurface = Color(0xFF1A1A1F),
    surfaceVariant = Color(0xFFE8E8EF),
    onSurfaceVariant = Color(0xFF45454F),
    error = Color(0xFFB3261E),
    onError = Color.White,
)

/**
 * Esquema oscuro.
 *
 * No es una inversion automatica del claro: en oscuro los azules saturados
 * vibran y cansan, asi que se aclaran y se desatura el fondo. Es el mismo
 * motivo por el que Material pide esquemas separados en vez de calcular uno
 * del otro.
 */
private val EsquemaOscuro = darkColorScheme(
    primary = LudoraAzulClaro,
    onPrimary = Color(0xFF00174A),
    primaryContainer = LudoraAzulOscuro,
    onPrimaryContainer = Color(0xFFDDE1FF),
    secondary = LudoraVioletaClaro,
    onSecondary = Color(0xFF2A0050),
    secondaryContainer = Color(0xFF4A2875),
    onSecondaryContainer = Color(0xFFEDDCFF),
    tertiary = Color(0xFFFF8A7A),
    onTertiary = Color(0xFF5F1410),
    background = Color(0xFF121216),
    onBackground = Color(0xFFE5E1E6),
    surface = Color(0xFF1A1A1F),
    onSurface = Color(0xFFE5E1E6),
    surfaceVariant = Color(0xFF45454F),
    onSurfaceVariant = Color(0xFFC7C5D0),
    error = Color(0xFFF2B8B5),
    onError = Color(0xFF601410),
)

/**
 * Tipografia.
 *
 * Solo se redefinen los estilos que la app usa de verdad; el resto los hereda
 * de Material. Se usa la fuente del sistema a proposito: meter una fuente
 * propia son cientos de kilobytes en el APK y un riesgo de que no tenga los
 * caracteres acentuados del espanol.
 */
private val TipografiaLudora = Typography(
    headlineMedium = TextStyle(
        fontWeight = FontWeight.Bold,
        fontSize = 26.sp,
        lineHeight = 32.sp,
    ),
    titleLarge = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 20.sp,
        lineHeight = 26.sp,
    ),
    titleMedium = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 16.sp,
        lineHeight = 22.sp,
    ),
    bodyLarge = TextStyle(
        fontWeight = FontWeight.Normal,
        fontSize = 16.sp,
        lineHeight = 24.sp,
    ),
    bodyMedium = TextStyle(
        fontWeight = FontWeight.Normal,
        fontSize = 14.sp,
        lineHeight = 20.sp,
    ),
    labelLarge = TextStyle(
        fontWeight = FontWeight.Medium,
        fontSize = 14.sp,
        lineHeight = 20.sp,
    ),
)

@Composable
fun TemaLudora(
    oscuro: Boolean = isSystemInDarkTheme(),
    contenido: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (oscuro) EsquemaOscuro else EsquemaClaro,
        typography = TipografiaLudora,
        content = contenido,
    )
}
