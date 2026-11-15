package com.ludora.apk.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.fragment.app.FragmentActivity
import com.ludora.apk.modelo.EstadoJuego
import com.ludora.apk.modelo.JuegoBiblioteca
import com.ludora.apk.modelo.JuegoTienda
import com.ludora.apk.seguridad.Biometria
import com.ludora.apk.ui.tema.Ludora

/**
 * La pantalla entera de Ludora en el telefono.
 *
 * ---------------------------------------------------------------------------
 * EL MISMO LUDORA DEL ESCRITORIO, EN VERTICAL
 * ---------------------------------------------------------------------------
 * Aqui no hay componentes de Material a la vista: todo se pinta como lo pinta
 * `HomeScene` en el PC. Rectangulos de color plano, esquinas rectas, marcos de
 * uno o dos pixeles, rotulos en mayusculas y sin acentos, y la paleta de
 * `Theme.h` (ver `tema/Tema.kt`).
 *
 * Lo que es IGUAL que en el escritorio:
 *   - la barra de titulo negra con el cuadrado rojo y "LUDORA";
 *   - la franja azul con los SEIS bloques, en el mismo orden y con los mismos
 *     indices, el activo en rojo LED con marco blanco;
 *   - cada seccion con sus colores, sus paneles y sus textos;
 *   - el avatar turquesa de la esquina inferior derecha;
 *   - la pantalla de CREAR SESION / INICIAR SESION.
 *
 * Lo que se ADAPTA, porque un telefono es estrecho y se toca con el dedo:
 *   - la franja del menu se desliza de lado (seis bloques no caben);
 *   - lo que en el PC son columnas, aqui son pisos, uno encima de otro;
 *   - los botones son mas altos (48 dp), que es lo que acierta un pulgar.
 *
 * ---------------------------------------------------------------------------
 * COMO SE DECIDE QUE SE PINTA
 * ---------------------------------------------------------------------------
 * Un solo `when` sobre `estado.acceso`, y dentro otro sobre la seccion. Sin
 * rutas ni grafo de navegacion: el estado ya es excluyente.
 */
@Composable
fun PantallaRaiz(
    estado: EstadoLudora,
    vm: VistaModeloLudora,
    actividad: FragmentActivity,
) {
    val snackbar = remember { SnackbarHostState() }

    // Los mensajes se ensenan y se consumen: si no se borrasen, volverian a
    // salir en cada recomposicion.
    LaunchedEffect(estado.mensaje) {
        val texto = estado.mensaje
        if (texto != null) {
            snackbar.showSnackbar(texto)
            vm.mensajeLeido()
        }
    }

    Scaffold(
        containerColor = Ludora.Bg,
        contentWindowInsets = WindowInsets(0, 0, 0, 0),
        snackbarHost = {
            SnackbarHost(snackbar) { dato ->
                Snackbar(
                    snackbarData = dato,
                    shape = RectangleShape,
                    containerColor = Ludora.BarBg,
                    contentColor = Ludora.Text,
                    modifier = Modifier.border(1.dp, Ludora.Border),
                )
            }
        },
    ) { margenes ->
        Column(
            Modifier
                .padding(margenes)
                .fillMaxSize()
                .background(Ludora.BarBg)
                .windowInsetsPadding(WindowInsets.statusBars)
                .windowInsetsPadding(WindowInsets.navigationBars),
        ) {
            BarraTitulo()
            Box(
                Modifier
                    .fillMaxSize()
                    .background(Ludora.Bg),
            ) {
                when (val acceso = estado.acceso) {
                    Acceso.Comprobando -> Cargando()
                    Acceso.SinCuenta -> PantallaCrearSesion(estado, vm)
                    is Acceso.Bloqueada -> PantallaDesbloqueo(acceso.email, estado, vm, actividad)
                    Acceso.PideContrasena -> PantallaIniciarSesion(estado, vm)
                    is Acceso.Dentro -> Dentro(acceso.email, estado, vm, actividad)
                }
            }
        }
    }
}

// ===========================================================================
// LA BARRA DE TITULO Y LA FRANJA DEL MENU
// ===========================================================================

/** El cuadrado rojo y "LUDORA", como en la ventana del escritorio. */
@Composable
private fun BarraTitulo() {
    Row(
        Modifier
            .fillMaxWidth()
            .height(40.dp)
            .background(Ludora.BarBg)
            .lineaAbajo(Ludora.Border)
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(12.dp)
                .background(Ludora.Accent),
        )
        Spacer(Modifier.width(9.dp))
        Text("LUDORA", color = Ludora.Text, fontSize = 20.sp)
    }
}

/**
 * La franja azul con los seis bloques.
 *
 * En el PC caben los seis a lo ancho; aqui la franja se desliza con el dedo.
 * El bloque activo es rojo LED con marco blanco, igual que alli.
 */
@Composable
private fun FranjaMenu(actual: Int, alElegir: (Int) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .background(Ludora.DarkBlue)
            .horizontalScroll(rememberScrollState())
            .padding(3.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Pestana.entries.forEach { p ->
            val activo = p.indice == actual
            Box(
                Modifier
                    .height(44.dp)
                    .width(if (p.titulo.length > 12) 176.dp else 124.dp)
                    .background(if (activo) Ludora.LedRed else Ludora.BlueDivider)
                    .then(if (activo) Modifier.border(1.dp, Ludora.White) else Modifier)
                    .clickable { alElegir(p.indice) },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    p.titulo,
                    color = if (activo) Ludora.White else Ludora.Glyph,
                    fontSize = 13.sp,
                    maxLines = 1,
                )
            }
        }
    }
}

// ===========================================================================
// ANTES DE ENTRAR
// ===========================================================================

@Composable
private fun Cargando() {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Text("LUDORA", color = Ludora.Dim, fontSize = 14.sp)
    }
}

/**
 * CREAR SESION: la misma pantalla del escritorio.
 *
 * Aqui NO se ofrece la biometria todavia, y es a proposito. La huella
 * desbloquea una sesion que ya existe; antes de que exista la cuenta no hay
 * nada que desbloquear. Se ofrece despues, desde CUENTA.
 */
@Composable
private fun PantallaCrearSesion(estado: EstadoLudora, vm: VistaModeloLudora) {
    var email by remember { mutableStateOf("") }
    var clave by remember { mutableStateOf("") }
    var repetida by remember { mutableStateOf("") }

    ColumnaFormulario("CREAR SESION") {
        CampoLudora("CORREO", email, { email = it }, "nombre@ludora.engine", KeyboardType.Email)
        CampoLudora("CONTRASENA", clave, { clave = it }, "contrasena", KeyboardType.Password, oculto = true)
        CampoLudora("REPITE LA CONTRASENA", repetida, { repetida = it }, "contrasena", KeyboardType.Password, oculto = true)
        Spacer(Modifier.height(18.dp))
        BotonMarco(
            "GUARDAR Y ENTRAR",
            linea = Ludora.Turquoise,
            activo = !estado.trabajando,
        ) { vm.crearCuenta(email.trim(), clave, repetida) }
        Spacer(Modifier.height(28.dp))
        Text(
            "El correo debe terminar en @ludora.engine",
            color = Ludora.Dim,
            fontSize = 12.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

/**
 * Desbloqueo con huella o cara.
 *
 * El boton de contrasena SIEMPRE esta: si el lector falla, si el usuario
 * registro una huella nueva (lo que invalida la clave) o si simplemente
 * prefiere escribirla, tiene que haber una salida.
 */
@Composable
private fun PantallaDesbloqueo(
    email: String,
    estado: EstadoLudora,
    vm: VistaModeloLudora,
    actividad: FragmentActivity,
) {
    ColumnaFormulario("INICIAR SESION") {
        Rotulo("CUENTA")
        Text(email, color = Ludora.Text, fontSize = 17.sp, modifier = Modifier.padding(top = 4.dp, bottom = 22.dp))
        BotonMarco(
            "ENTRAR CON HUELLA O CARA",
            linea = Ludora.Turquoise,
            activo = !estado.trabajando,
        ) { vm.desbloquearConBiometria(actividad) }
        Spacer(Modifier.height(12.dp))
        BotonMarco("ENTRAR CON LA CONTRASENA", linea = Ludora.Border, texto = Ludora.Glyph) {
            vm.cerrarSesion()
        }
    }
}

@Composable
private fun PantallaIniciarSesion(estado: EstadoLudora, vm: VistaModeloLudora) {
    var email by remember { mutableStateOf("") }
    var clave by remember { mutableStateOf("") }

    ColumnaFormulario("INICIAR SESION") {
        CampoLudora("CORREO", email, { email = it }, "nombre@ludora.engine", KeyboardType.Email)
        CampoLudora("CONTRASENA", clave, { clave = it }, "contrasena", KeyboardType.Password, oculto = true)
        Spacer(Modifier.height(18.dp))
        BotonMarco(
            "ENTRAR",
            linea = Ludora.Turquoise,
            activo = !estado.trabajando,
        ) { vm.entrarConContrasena(email.trim(), clave) }
    }
}

// ===========================================================================
// YA DENTRO
// ===========================================================================

@Composable
private fun Dentro(
    email: String,
    estado: EstadoLudora,
    vm: VistaModeloLudora,
    actividad: FragmentActivity,
) {
    val seccion = Pestana.de(estado.pestana)
    Column(Modifier.fillMaxSize()) {
        FranjaMenu(seccion.indice, vm::irAPestana)
        Box(Modifier.fillMaxSize()) {
            // Una ficha abierta tapa la tienda: en un telefono no caben las dos.
            val juego = estado.juegoAbierto
            if (juego != null) {
                FichaJuego(juego, estado, vm)
            } else {
                when (seccion) {
                    Pestana.TIENDA -> SeccionTienda(estado, vm)
                    Pestana.CHAT -> SeccionChat()
                    Pestana.BIBLIOTECA -> SeccionBiblioteca(email, estado, vm)
                    Pestana.COMUNIDADES -> SeccionVacia(seccion.titulo)
                    Pestana.ESTADISTICAS -> SeccionVacia(seccion.titulo)
                    Pestana.CUENTA -> SeccionCuenta(email, estado, vm, actividad)
                }
            }
            // El avatar de la esquina, como en el escritorio. En CUENTA no
            // sale: alli taparia los botones del final.
            if (seccion != Pestana.CUENTA && juego == null) {
                Avatar(email, conCorreo = seccion != Pestana.BIBLIOTECA) {
                    vm.irAPestana(Pestana.CUENTA.indice)
                }
            }
        }
    }
}

/** El circulo turquesa de abajo a la derecha, con el correo debajo. */
@Composable
private fun BoxScope.Avatar(email: String, conCorreo: Boolean, alPulsar: () -> Unit) {
    Column(
        Modifier
            .align(Alignment.BottomEnd)
            .padding(end = 12.dp, bottom = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .size(68.dp)
                .clip(CircleShape)
                .background(Ludora.PanelDeep)
                .border(3.dp, Ludora.Turquoise, CircleShape)
                .clickable(onClick = alPulsar),
            contentAlignment = Alignment.Center,
        ) {
            Text("?", color = Ludora.Turquoise, fontSize = 20.sp)
        }
        if (conCorreo) {
            Text(email, color = Ludora.Dim, fontSize = 11.sp, modifier = Modifier.padding(top = 3.dp))
        }
    }
}

// --- TIENDA ----------------------------------------------------------------

/**
 * La tienda: el panel grande del destacado, con marco rojo, y debajo los tres
 * de PROXIMAMENTE, OFERTAS y RECIEN PUBLICADOS. En el PC van en fila; aqui,
 * uno debajo de otro.
 */
@Composable
private fun SeccionTienda(estado: EstadoLudora, vm: VistaModeloLudora) {
    val juegos = estado.catalogo
    Column(
        Modifier
            .fillMaxSize()
            .background(Ludora.SteamBg)
            .verticalScroll(rememberScrollState())
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // El destacado.
        val destacado = juegos.firstOrNull()
        Box(
            Modifier
                .fillMaxWidth()
                .height(190.dp)
                .background(Ludora.PanelDeep)
                .border(2.dp, Ludora.Border)
                .then(if (destacado != null) Modifier.clickable { vm.abrirJuego(destacado) } else Modifier)
                .padding(16.dp),
            contentAlignment = Alignment.Center,
        ) {
            if (destacado == null) {
                Text("Aun no hay juegos en la tienda", color = Ludora.Dim, fontSize = 17.sp, textAlign = TextAlign.Center)
            } else {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(destacado.nombre, color = Ludora.Text, fontSize = 22.sp, textAlign = TextAlign.Center)
                    Text(destacado.generos, color = Ludora.SteamDim, fontSize = 13.sp, modifier = Modifier.padding(top = 6.dp))
                    Text(destacado.precioTexto, color = Ludora.SteamGreen, fontSize = 17.sp, modifier = Modifier.padding(top = 10.dp))
                }
            }
        }

        // Sin servidor no hay fechas de salida ni descuentos: esos dos
        // paneles estan, como en el PC, y dicen que no tienen juegos.
        PanelTienda("PROXIMAMENTE", emptyList(), vm)
        PanelTienda("OFERTAS", emptyList(), vm)
        PanelTienda("RECIEN PUBLICADOS", juegos, vm)
        Spacer(Modifier.height(86.dp))      // sitio para el avatar
    }
}

@Composable
private fun PanelTienda(titulo: String, juegos: List<JuegoTienda>, vm: VistaModeloLudora) {
    Column(
        Modifier
            .fillMaxWidth()
            .heightIn(min = 132.dp)
            .background(Ludora.PanelViolet)
            .border(1.dp, Ludora.PanelEdge)
            .padding(8.dp),
    ) {
        Text(titulo, color = Ludora.Accent, fontSize = 13.sp)
        if (juegos.isEmpty()) {
            Box(
                Modifier
                    .fillMaxWidth()
                    .height(96.dp),
                contentAlignment = Alignment.Center,
            ) { Text("(sin juegos)", color = Ludora.Dim, fontSize = 13.sp) }
        } else {
            juegos.forEach { juego ->
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp)
                        .background(Ludora.SteamPanel)
                        .border(1.dp, Ludora.SteamEdge)
                        .clickable { vm.abrirJuego(juego) }
                        .padding(horizontal = 12.dp, vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(juego.nombre, color = Ludora.SteamText, fontSize = 16.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(juego.generos, color = Ludora.SteamDim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    Text(juego.precioTexto, color = Ludora.SteamGreen, fontSize = 15.sp, modifier = Modifier.padding(start = 10.dp))
                }
            }
        }
    }
}

/** La ficha de un juego, con los azules de la tienda. */
@Composable
private fun FichaJuego(juego: JuegoTienda, estado: EstadoLudora, vm: VistaModeloLudora) {
    Column(
        Modifier
            .fillMaxSize()
            .background(Ludora.SteamBg)
            .verticalScroll(rememberScrollState())
            .padding(14.dp),
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .background(Ludora.PanelDeep)
                .border(2.dp, Ludora.Border)
                .padding(16.dp),
        ) {
            Text(juego.nombre, color = Ludora.Text, fontSize = 22.sp)
            Text(juego.generos, color = Ludora.SteamBlue, fontSize = 13.sp, modifier = Modifier.padding(top = 6.dp))
            Text(juego.precioTexto, color = Ludora.SteamGreen, fontSize = 20.sp, modifier = Modifier.padding(top = 12.dp))
        }
        Spacer(Modifier.height(12.dp))
        Column(
            Modifier
                .fillMaxWidth()
                .background(Ludora.SteamPanel)
                .border(1.dp, Ludora.SteamEdge)
                .padding(14.dp),
        ) {
            Text(juego.descripcion, color = Ludora.SteamText, fontSize = 15.sp, lineHeight = 21.sp)
            Spacer(Modifier.height(12.dp))
            Text("TAMANO  ${enMegas(juego.tamanoBytes)}", color = Ludora.SteamDim, fontSize = 13.sp)
            Text("EDAD  de ${juego.edadMinima} a ${juego.edadMaxima} anos", color = Ludora.SteamDim, fontSize = 13.sp)
        }
        Spacer(Modifier.height(18.dp))
        BotonMarco(
            if (juego.precioCentavos == 0) "CONSEGUIR GRATIS" else "COMPRAR",
            linea = Ludora.SteamGreen,
            activo = !estado.trabajando,
        ) { vm.conseguirJuego(juego) }
        Spacer(Modifier.height(10.dp))
        BotonMarco("VOLVER", linea = Ludora.SteamEdge, texto = Ludora.SteamText) { vm.cerrarJuego() }
    }
}

// --- CHAT ------------------------------------------------------------------

/**
 * El chat: la tira violeta de conversaciones y el panel en vino.
 *
 * Sin servidor no hay con quien hablar, y se dice en vez de pintar mensajes
 * de mentira.
 */
@Composable
private fun SeccionChat() {
    Row(Modifier.fillMaxSize()) {
        Row(
            Modifier
                .width(92.dp)
                .fillMaxHeight()
                .background(Ludora.PanelDeep),
        ) {
            Box(
                Modifier
                    .width(8.dp)
                    .fillMaxHeight(0.55f)
                    .background(Ludora.ScrollThumb),
            )
            Text("(sin chats)", color = Ludora.Dim, fontSize = 12.sp, modifier = Modifier.padding(start = 8.dp, top = 14.dp))
        }
        Box(
            Modifier
                .fillMaxSize()
                .background(Ludora.Wine)
                .lineaIzquierda(Ludora.WineEdge)
                .padding(20.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                "Aun no hay conversaciones.\nEl chat llegara cuando Ludora tenga servidor.",
                color = Ludora.Glyph,
                fontSize = 14.sp,
                textAlign = TextAlign.Center,
            )
        }
    }
}

// --- BIBLIOTECA ------------------------------------------------------------

/**
 * La biblioteca: la lista violeta, el panel negro del juego y el panel rojo de
 * las acciones. En el PC son tres columnas; aqui, tres pisos.
 */
@Composable
private fun SeccionBiblioteca(email: String, estado: EstadoLudora, vm: VistaModeloLudora) {
    var elegido by remember { mutableStateOf<String?>(null) }
    var confirmarBorrado by remember { mutableStateOf(false) }
    val juego = estado.biblioteca.firstOrNull { it.codigo == elegido }

    Column(Modifier.fillMaxSize()) {
        // 1. La lista, con su barra de desplazamiento a la izquierda.
        Row(
            Modifier
                .fillMaxWidth()
                .weight(1.15f)
                .background(Ludora.PanelDeep),
        ) {
            Box(
                Modifier
                    .width(12.dp)
                    .fillMaxHeight()
                    .background(Ludora.ScrollTrack),
            ) {
                Box(
                    Modifier
                        .padding(start = 2.dp)
                        .width(8.dp)
                        .fillMaxHeight(0.6f)
                        .background(Ludora.ScrollThumb),
                )
            }
            Column(
                Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(horizontal = 10.dp),
            ) {
                val huecos = maxOf(4, estado.biblioteca.size)
                for (i in 0 until huecos) {
                    val j = estado.biblioteca.getOrNull(i)
                    val marcado = j != null && j.codigo == elegido
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .height(60.dp)
                            .lineaArriba(Ludora.PanelEdge, 2.dp)
                            .background(if (marcado) Ludora.PanelViolet else Color.Transparent)
                            .then(if (j != null) Modifier.clickable { elegido = j.codigo } else Modifier)
                            .padding(horizontal = 12.dp),
                        contentAlignment = Alignment.CenterStart,
                    ) {
                        if (j == null) {
                            Text("(vacio)", color = Ludora.Dim, fontSize = 13.sp)
                        } else {
                            Text(j.nombre, color = Ludora.Text, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                    }
                }
            }
        }

        // 2. El juego elegido, sobre negro.
        Box(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(Ludora.Bg)
                .padding(16.dp),
            contentAlignment = Alignment.Center,
        ) {
            if (juego == null) {
                Text("Selecciona un juego de la biblioteca", color = Ludora.Dim, fontSize = 13.sp, textAlign = TextAlign.Center)
            } else {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(juego.nombre, color = Ludora.Text, fontSize = 20.sp, textAlign = TextAlign.Center)
                    Text(textoEstado(juego.estado), color = Ludora.Glyph, fontSize = 13.sp, modifier = Modifier.padding(top = 6.dp))
                    if (juego.estado == EstadoJuego.DESCARGANDO) {
                        Box(
                            Modifier
                                .padding(top = 12.dp)
                                .fillMaxWidth()
                                .height(8.dp)
                                .background(Ludora.FieldBg)
                                .border(1.dp, Ludora.FieldBorder),
                        ) {
                            Box(
                                Modifier
                                    .fillMaxHeight()
                                    .fillMaxWidth(juego.progreso)
                                    .background(Ludora.Accent),
                            )
                        }
                    }
                }
            }
            Text(
                email,
                color = Ludora.Dim,
                fontSize = 12.sp,
                modifier = Modifier.align(Alignment.BottomCenter),
            )
        }

        // 3. Las acciones, sobre rojo.
        Column(
            Modifier
                .fillMaxWidth()
                .weight(0.9f)
                .background(Ludora.LedRed)
                .padding(start = 16.dp, top = 12.dp, bottom = 12.dp, end = 96.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (juego == null) {
                Text("Elige un juego", color = Ludora.White, fontSize = 13.sp)
            } else {
                when (juego.estado) {
                    EstadoJuego.PENDIENTE, EstadoJuego.FALLIDO ->
                        BotonMarco("DESCARGAR", linea = Ludora.White, relleno = Ludora.LedRed, activo = !estado.trabajando) {
                            vm.descargar(juego.codigo)
                        }
                    EstadoJuego.INSTALADO ->
                        BotonMarco("DESINSTALAR", linea = Ludora.White, relleno = Ludora.LedRed) { vm.desinstalar(juego.codigo) }
                    EstadoJuego.DESINSTALADO ->
                        BotonMarco("REINSTALAR", linea = Ludora.White, relleno = Ludora.LedRed) { vm.reinstalar(juego.codigo) }
                    EstadoJuego.DESCARGANDO -> Unit
                }
                Spacer(Modifier.height(8.dp))
                BotonMarco("QUITAR", linea = Ludora.White, relleno = Ludora.LedRed) { confirmarBorrado = true }
            }
        }
    }

    // Quitar SI borra los archivos, al contrario que desinstalar. Por eso se
    // pregunta: es la unica accion de esta pantalla que no se puede deshacer.
    if (confirmarBorrado && juego != null) {
        Confirmar(
            titulo = "QUITAR ${juego.nombre.uppercase()}",
            texto = "Se borrara el paquete del telefono. Esto no se puede deshacer, " +
                "aunque podras volver a conseguirlo en la Tienda.",
            si = "QUITAR",
            alCerrar = { confirmarBorrado = false },
        ) {
            vm.eliminarDeBiblioteca(juego.codigo)
            elegido = null
        }
    }
}

// --- COMUNIDADES / ESTADISTICAS & HORAS --------------------------------------

/** Una seccion sin contenido: su nombre en azul casi negro, como en el PC, y "seccion vacia". */
@Composable
private fun SeccionVacia(titulo: String) {
    Box(
        Modifier
            .fillMaxSize()
            .background(Ludora.Bg),
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(titulo, color = Ludora.BlueDivider, fontSize = 22.sp, textAlign = TextAlign.Center)
            Text("seccion vacia", color = Ludora.Dim, fontSize = 13.sp, modifier = Modifier.padding(top = 8.dp))
        }
    }
}

// --- CUENTA ----------------------------------------------------------------

/**
 * La cuenta: las mismas tarjetas violetas del escritorio (foto y nombre,
 * cuenta, contrasena, descripcion publica) y el boton CERRAR SESION.
 *
 * Lo que el telefono anade es la tarjeta de HUELLA O CARA, que el PC no tiene
 * porque alli lo que se verifica es el equipo.
 */
@Composable
private fun SeccionCuenta(
    email: String,
    estado: EstadoLudora,
    vm: VistaModeloLudora,
    actividad: FragmentActivity,
) {
    var confirmarBorrado by remember { mutableStateOf(false) }

    Column(
        Modifier
            .fillMaxSize()
            .background(Ludora.Bg)
            .verticalScroll(rememberScrollState())
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        // Foto y nombre.
        TarjetaCuenta {
            Row {
                Box(
                    Modifier
                        .size(92.dp)
                        .background(Ludora.PanelDeep)
                        .border(1.dp, Ludora.PanelEdge),
                    contentAlignment = Alignment.Center,
                ) { Text("SIN FOTO", color = Ludora.Dim, fontSize = 12.sp) }
                Column(Modifier.padding(start = 14.dp)) {
                    Rotulo("NOMBRE DE USUARIO")
                    Text(
                        email.substringBefore('@'),
                        color = Ludora.Text,
                        fontSize = 17.sp,
                        modifier = Modifier.padding(top = 4.dp, bottom = 12.dp),
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        BotonPanel("GALERIA") { vm.avisarPendiente("La foto de perfil todavia no se puede cambiar desde el telefono.") }
                        BotonPanel("CAMARA") { vm.avisarPendiente("La foto de perfil todavia no se puede cambiar desde el telefono.") }
                    }
                }
            }
        }
        TarjetaCuenta {
            Rotulo("CUENTA")
            Text(email, color = Ludora.Text, fontSize = 17.sp, modifier = Modifier.padding(top = 4.dp))
        }
        TarjetaCuenta {
            Rotulo("CONTRASENA")
            Text("**********", color = Ludora.Glyph, fontSize = 15.sp, modifier = Modifier.padding(top = 6.dp))
        }
        TarjetaCuenta {
            Rotulo("DESCRIPCION PUBLICA")
            Text("(vacia)", color = Ludora.Glyph, fontSize = 13.sp, modifier = Modifier.padding(top = 6.dp))
        }

        // Solo en el telefono: entrar con el lector.
        TarjetaCuenta {
            Rotulo("HUELLA O CARA")
            Text(
                "Tu huella y tu cara NO se guardan: Android no se las da a ninguna " +
                    "aplicacion. Lo que se guarda es tu sesion, cifrada, y el lector " +
                    "del telefono es quien la desbloquea.",
                color = Ludora.Glyph,
                fontSize = 13.sp,
                lineHeight = 18.sp,
                modifier = Modifier.padding(top = 6.dp, bottom = 12.dp),
            )
            if (estado.biometria == Biometria.Disponibilidad.LISTA) {
                BotonMarco(
                    if (estado.biometriaActiva) "ACTIVADO  -  TOCAR PARA DESACTIVAR" else "ACTIVAR",
                    linea = Ludora.Turquoise,
                    relleno = Ludora.PanelDeep,
                    activo = !estado.trabajando,
                ) {
                    if (estado.biometriaActiva) vm.desactivarBiometria() else vm.activarBiometria(actividad)
                }
            } else {
                Text(textoBiometriaNoLista(estado.biometria), color = Ludora.Accent, fontSize = 13.sp)
            }
        }

        BotonMarco("CERRAR SESION", linea = Ludora.Border, texto = Ludora.Glyph) { vm.cerrarSesion() }
        BotonMarco("BORRAR LA CUENTA", linea = Ludora.FieldBorder, texto = Ludora.Dim) { confirmarBorrado = true }
        Text(
            email,
            color = Ludora.Dim,
            fontSize = 11.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth(),
        )
    }

    if (confirmarBorrado) {
        Confirmar(
            titulo = "BORRAR LA CUENTA",
            texto = "Se borrara la cuenta de este telefono, con su biblioteca. Esto no se puede deshacer.",
            si = "BORRAR",
            alCerrar = { confirmarBorrado = false },
        ) { vm.borrarCuenta() }
    }
}

/** Una tarjeta violeta con su linea mas clara arriba, como en el escritorio. */
@Composable
private fun TarjetaCuenta(contenido: @Composable () -> Unit) {
    Column(
        Modifier
            .fillMaxWidth()
            .background(Ludora.PanelViolet)
            .lineaArriba(Ludora.PanelEdge, 2.dp)
            .padding(16.dp),
    ) { contenido() }
}

// ===========================================================================
// PIEZAS SUELTAS
// ===========================================================================

/** El titulo con su subrayado rojo y el formulario centrado debajo. */
@Composable
private fun ColumnaFormulario(titulo: String, contenido: @Composable () -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 28.dp, vertical = 24.dp),
        ) {
            Column(
                Modifier.align(Alignment.CenterHorizontally),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(titulo, color = Ludora.Text, fontSize = 22.sp)
                Box(
                    Modifier
                        .padding(top = 8.dp)
                        .width(150.dp)
                        .height(2.dp)
                        .background(Ludora.Accent),
                )
            }
            Spacer(Modifier.height(26.dp))
            contenido()
        }
    }
}

/** Un rotulo pequeno, gris, en mayusculas: la etiqueta de un campo. */
@Composable
private fun Rotulo(texto: String) {
    Text(texto, color = Ludora.Dim, fontSize = 12.sp)
}

/**
 * Un campo de texto como los del escritorio: caja casi negra con marco
 * granate, que al tener el foco se enciende en rojo.
 */
@Composable
private fun CampoLudora(
    etiqueta: String,
    valor: String,
    alCambiar: (String) -> Unit,
    pista: String,
    teclado: KeyboardType,
    oculto: Boolean = false,
) {
    var conFoco by remember { mutableStateOf(false) }
    Rotulo(etiqueta)
    BasicTextField(
        value = valor,
        onValueChange = alCambiar,
        singleLine = true,
        textStyle = TextStyle(color = Ludora.Text, fontSize = 17.sp),
        cursorBrush = SolidColor(Ludora.Accent),
        keyboardOptions = KeyboardOptions(keyboardType = teclado),
        visualTransformation = if (oculto) PasswordVisualTransformation() else VisualTransformation.None,
        modifier = Modifier
            .padding(top = 3.dp, bottom = 12.dp)
            .fillMaxWidth()
            .height(48.dp)
            .background(if (conFoco) Ludora.FieldBgFocus else Ludora.FieldBg)
            .border(if (conFoco) 2.dp else 1.dp, if (conFoco) Ludora.Accent else Ludora.FieldBorder)
            .onFocusChanged { conFoco = it.isFocused },
        decorationBox = { campo ->
            Box(Modifier.padding(horizontal = 10.dp), contentAlignment = Alignment.CenterStart) {
                if (valor.isEmpty()) Text(pista, color = Ludora.Dim, fontSize = 17.sp)
                campo()
            }
        },
    )
}

/**
 * El boton de Ludora: un rectangulo oscuro con marco de color y el rotulo en
 * ese mismo color. Sin esquinas redondas, sin sombra.
 */
@Composable
private fun BotonMarco(
    rotulo: String,
    linea: Color,
    texto: Color = linea,
    relleno: Color = Ludora.FieldBg,
    activo: Boolean = true,
    alto: Dp = 48.dp,
    alPulsar: () -> Unit,
) {
    Box(
        Modifier
            .fillMaxWidth()
            .height(alto)
            .background(relleno)
            .border(2.dp, if (activo) linea else Ludora.FieldBorder)
            .then(if (activo) Modifier.clickable(onClick = alPulsar) else Modifier),
        contentAlignment = Alignment.Center,
    ) {
        Text(rotulo, color = if (activo) texto else Ludora.Dim, fontSize = 13.sp, maxLines = 1)
    }
}

/** Los botones pequenos de dentro de un panel violeta (GALERIA, CAMARA). */
@Composable
private fun BotonPanel(rotulo: String, alPulsar: () -> Unit) {
    Box(
        Modifier
            .height(40.dp)
            .width(92.dp)
            .background(Ludora.PanelDeep)
            .border(1.dp, Ludora.PanelEdge)
            .clickable(onClick = alPulsar),
        contentAlignment = Alignment.Center,
    ) { Text(rotulo, color = Ludora.Text, fontSize = 13.sp) }
}

/** Una pregunta de si o no, con la cara de Ludora en vez de la de Material. */
@Composable
private fun Confirmar(
    titulo: String,
    texto: String,
    si: String,
    alCerrar: () -> Unit,
    alAceptar: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = alCerrar,
        shape = RectangleShape,
        containerColor = Ludora.BarBg,
        titleContentColor = Ludora.Text,
        textContentColor = Ludora.Glyph,
        modifier = Modifier.border(1.dp, Ludora.Border),
        title = { Text(titulo, fontSize = 17.sp) },
        text = { Text(texto, fontSize = 14.sp, lineHeight = 20.sp) },
        confirmButton = {
            Box(Modifier.width(120.dp)) {
                BotonMarco(si, linea = Ludora.Accent, alto = 44.dp) {
                    alCerrar()
                    alAceptar()
                }
            }
        },
        dismissButton = {
            Box(Modifier.width(120.dp)) {
                BotonMarco("CANCELAR", linea = Ludora.FieldBorder, texto = Ludora.Glyph, alto = 44.dp, alPulsar = alCerrar)
            }
        },
    )
}

// --- lineas de un pixel, que es como separa las cosas el escritorio ---------

private fun Modifier.lineaAbajo(color: Color, grosor: Dp = 1.dp) = drawBehind {
    val g = grosor.toPx()
    drawLine(color, Offset(0f, size.height - g / 2), Offset(size.width, size.height - g / 2), g)
}

private fun Modifier.lineaArriba(color: Color, grosor: Dp = 1.dp) = drawBehind {
    val g = grosor.toPx()
    drawLine(color, Offset(0f, g / 2), Offset(size.width, g / 2), g)
}

private fun Modifier.lineaIzquierda(color: Color, grosor: Dp = 2.dp) = drawBehind {
    val g = grosor.toPx()
    drawLine(color, Offset(g / 2, 0f), Offset(g / 2, size.height), g)
}

private fun textoEstado(estado: EstadoJuego): String = when (estado) {
    EstadoJuego.PENDIENTE -> "Sin descargar"
    EstadoJuego.DESCARGANDO -> "Descargando..."
    EstadoJuego.INSTALADO -> "Instalado"
    EstadoJuego.DESINSTALADO -> "Desinstalado (los archivos siguen aqui)"
    EstadoJuego.FALLIDO -> "La descarga fallo"
}

private fun textoBiometriaNoLista(d: Biometria.Disponibilidad): String = when (d) {
    Biometria.Disponibilidad.SIN_REGISTRAR ->
        "Registra una huella o tu cara en los ajustes del telefono para poder usarlo."
    Biometria.Disponibilidad.SIN_SOPORTE ->
        "Este telefono no tiene lector de huella ni cara compatible."
    Biometria.Disponibilidad.NO_DISPONIBLE ->
        "El lector no esta disponible ahora mismo."
    Biometria.Disponibilidad.LISTA -> ""
}

private fun enMegas(bytes: Long): String =
    if (bytes <= 0) "desconocido"
    else "%.1f MB".format(bytes / 1048576.0)
