package com.ludora.apk.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.fragment.app.FragmentActivity
import com.ludora.apk.modelo.EstadoJuego
import com.ludora.apk.modelo.JuegoBiblioteca
import com.ludora.apk.modelo.JuegoTienda
import com.ludora.apk.seguridad.Biometria

/**
 * La pantalla entera de Ludora en el telefono.
 *
 * ---------------------------------------------------------------------------
 * COMO SE DECIDE QUE SE PINTA
 * ---------------------------------------------------------------------------
 * Un solo `when` sobre `estado.acceso`. No hay navegacion con rutas ni un
 * grafo de pantallas: el estado de acceso ya es excluyente (ver `Acceso` en
 * VistaModeloLudora), asi que un `when` basta y no se puede llegar a dos
 * pantallas a la vez.
 *
 * Dentro de la app ya, las pestanas son lo mismo: un indice en el estado, que
 * ademas se guarda en disco. Por eso cerrar la app en Biblioteca y volver a
 * abrirla devuelve a Biblioteca, igual que en el escritorio.
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
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            // La barra de pestanas solo tiene sentido una vez dentro.
            if (estado.acceso is Acceso.Dentro) {
                BarraPestanas(
                    actual = estado.pestana,
                    alElegir = vm::irAPestana,
                )
            }
        },
    ) { margenes ->
        Box(Modifier.padding(margenes)) {
            when (val acceso = estado.acceso) {
                Acceso.Comprobando -> Cargando()

                Acceso.SinCuenta -> PantallaCrearCuenta(estado, vm)

                is Acceso.Bloqueada -> PantallaDesbloqueo(
                    email = acceso.email,
                    estado = estado,
                    vm = vm,
                    actividad = actividad,
                )

                Acceso.PideContrasena -> PantallaContrasena(estado, vm)

                is Acceso.Dentro -> Dentro(
                    email = acceso.email,
                    estado = estado,
                    vm = vm,
                    actividad = actividad,
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
        CircularProgressIndicator()
    }
}

/**
 * Crear la cuenta: correo y contrasena dos veces.
 *
 * Aqui NO se ofrece la biometria todavia, y es a proposito. La huella
 * desbloquea una sesion que ya existe; antes de que exista la cuenta no hay
 * nada que desbloquear. Se ofrece despues, desde Cuenta.
 */
@Composable
private fun PantallaCrearCuenta(estado: EstadoLudora, vm: VistaModeloLudora) {
    var email by remember { mutableStateOf("") }
    var clave by remember { mutableStateOf("") }
    var repetida by remember { mutableStateOf("") }

    ColumnaFormulario {
        Text("Crear tu cuenta", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))
        Text(
            "Es la cuenta de Ludora en este telefono. Despues podras entrar " +
                "con la huella o la cara.",
            style = MaterialTheme.typography.bodyMedium,
        )
        Spacer(Modifier.height(24.dp))

        OutlinedTextField(
            value = email,
            onValueChange = { email = it },
            label = { Text("Correo") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email),
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(12.dp))
        CampoClave(clave, { clave = it }, "Contrasena")
        Spacer(Modifier.height(12.dp))
        CampoClave(repetida, { repetida = it }, "Repite la contrasena")

        Spacer(Modifier.height(24.dp))
        Button(
            onClick = { vm.crearCuenta(email.trim(), clave, repetida) },
            enabled = !estado.trabajando,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Crear cuenta") }
    }
}

/**
 * Desbloqueo con huella o cara.
 *
 * El boton de contrasena SIEMPRE esta: si el lector falla, si el usuario
 * registro una huella nueva (lo que invalida la clave) o si simplemente
 * prefiere escribirla, tiene que haber una salida. Sin esto, un fallo del
 * sensor dejaria al usuario fuera de su propia cuenta.
 */
@Composable
private fun PantallaDesbloqueo(
    email: String,
    estado: EstadoLudora,
    vm: VistaModeloLudora,
    actividad: FragmentActivity,
) {
    ColumnaFormulario(centrado = true) {
        Text("Ludora", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(8.dp))
        Text(email, style = MaterialTheme.typography.bodyMedium)
        Spacer(Modifier.height(32.dp))

        Button(
            onClick = { vm.desbloquearConBiometria(actividad) },
            enabled = !estado.trabajando,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Entrar con huella o cara") }

        Spacer(Modifier.height(12.dp))
        TextButton(onClick = { vm.cerrarSesion() }) {
            Text("Entrar con la contrasena")
        }
    }
}

@Composable
private fun PantallaContrasena(estado: EstadoLudora, vm: VistaModeloLudora) {
    var email by remember { mutableStateOf("") }
    var clave by remember { mutableStateOf("") }

    ColumnaFormulario {
        Text("Entrar", style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(24.dp))

        OutlinedTextField(
            value = email,
            onValueChange = { email = it },
            label = { Text("Correo") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email),
            modifier = Modifier.fillMaxWidth(),
        )
        Spacer(Modifier.height(12.dp))
        CampoClave(clave, { clave = it }, "Contrasena")

        Spacer(Modifier.height(24.dp))
        Button(
            onClick = { vm.entrarConContrasena(email.trim(), clave) },
            enabled = !estado.trabajando,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Entrar") }
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
    // Una ficha abierta tapa la pestana: en un telefono no caben las dos.
    val juego = estado.juegoAbierto
    if (juego != null) {
        FichaJuego(juego, estado, vm)
        return
    }

    when (Pestana.de(estado.pestana)) {
        Pestana.TIENDA -> PestanaTienda(estado, vm)
        Pestana.BIBLIOTECA -> PestanaBiblioteca(estado, vm)
        Pestana.CUENTA -> PestanaCuenta(email, estado, vm, actividad)
    }
}

@Composable
private fun BarraPestanas(actual: Int, alElegir: (Int) -> Unit) {
    NavigationBar {
        Pestana.entries.forEach { p ->
            NavigationBarItem(
                selected = p.indice == actual,
                onClick = { alElegir(p.indice) },
                icon = {},
                label = { Text(p.titulo) },
            )
        }
    }
}

// --- Tienda ----------------------------------------------------------------

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PestanaTienda(estado: EstadoLudora, vm: VistaModeloLudora) {
    Column {
        CenterAlignedTopAppBar(title = { Text("Tienda") })

        if (estado.catalogo.isEmpty()) {
            Vacio(
                "No hay juegos que ensenar todavia.",
                "El catalogo llegara cuando Ludora tenga servidor.",
            )
            return
        }

        LazyColumn(
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            items(estado.catalogo, key = { it.codigo }) { juego ->
                TarjetaTienda(juego) { vm.abrirJuego(juego) }
            }
        }
    }
}

@Composable
private fun TarjetaTienda(juego: JuegoTienda, alPulsar: () -> Unit) {
    Card(
        onClick = alPulsar,
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(Modifier.padding(16.dp)) {
            Text(juego.nombre, style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(4.dp))
            Text(
                juego.generos,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(8.dp))
            Text(
                juego.precioTexto,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary,
            )
        }
    }
}

/**
 * Ficha de un juego.
 *
 * Un solo boton grande, que es lo que pidio el usuario: "comprar mas facil".
 * El escritorio reparte esto en varios pasos; aqui es tocar una vez.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun FichaJuego(
    juego: JuegoTienda,
    estado: EstadoLudora,
    vm: VistaModeloLudora,
) {
    Column(Modifier.fillMaxSize()) {
        CenterAlignedTopAppBar(title = { Text(juego.nombre) })

        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        ) {
            Text(juego.precioTexto, style = MaterialTheme.typography.headlineMedium)
            Spacer(Modifier.height(16.dp))
            Text(juego.descripcion, style = MaterialTheme.typography.bodyLarge)
            Spacer(Modifier.height(16.dp))

            Text(
                "Generos: ${juego.generos}",
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(
                "Tamano: ${enMegas(juego.tamanoBytes)}",
                style = MaterialTheme.typography.bodyMedium,
            )
            Text(
                "Edad: de ${juego.edadMinima} a ${juego.edadMaxima} anos",
                style = MaterialTheme.typography.bodyMedium,
            )

            Spacer(Modifier.height(32.dp))
            Button(
                onClick = { vm.conseguirJuego(juego) },
                enabled = !estado.trabajando,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(if (juego.precioCentavos == 0) "Conseguir gratis" else "Comprar")
            }
            Spacer(Modifier.height(8.dp))
            OutlinedButton(
                onClick = { vm.cerrarJuego() },
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Volver") }
        }
    }
}

// --- Biblioteca ------------------------------------------------------------

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PestanaBiblioteca(estado: EstadoLudora, vm: VistaModeloLudora) {
    Column {
        CenterAlignedTopAppBar(title = { Text("Biblioteca") })

        if (estado.biblioteca.isEmpty()) {
            Vacio(
                "Tu biblioteca esta vacia.",
                "Lo que consigas en la Tienda aparecera aqui.",
            )
            return
        }

        LazyColumn(
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            items(estado.biblioteca, key = { it.codigo }) { juego ->
                TarjetaBiblioteca(juego, estado, vm)
            }
        }
    }
}

@Composable
private fun TarjetaBiblioteca(
    juego: JuegoBiblioteca,
    estado: EstadoLudora,
    vm: VistaModeloLudora,
) {
    var confirmarBorrado by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(Modifier.padding(16.dp)) {
            Text(juego.nombre, style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(4.dp))
            Text(
                textoEstado(juego.estado),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            if (juego.estado == EstadoJuego.DESCARGANDO) {
                Spacer(Modifier.height(8.dp))
                LinearProgressIndicator(
                    progress = { juego.progreso },
                    modifier = Modifier.fillMaxWidth(),
                )
            }

            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                when (juego.estado) {
                    EstadoJuego.PENDIENTE, EstadoJuego.FALLIDO ->
                        FilledTonalButton(
                            onClick = { vm.descargar(juego.codigo) },
                            enabled = !estado.trabajando,
                        ) { Text("Descargar") }

                    EstadoJuego.INSTALADO ->
                        OutlinedButton(onClick = { vm.desinstalar(juego.codigo) }) {
                            Text("Desinstalar")
                        }

                    EstadoJuego.DESINSTALADO ->
                        FilledTonalButton(onClick = { vm.reinstalar(juego.codigo) }) {
                            Text("Reinstalar")
                        }

                    EstadoJuego.DESCARGANDO -> Unit
                }

                TextButton(onClick = { confirmarBorrado = true }) { Text("Quitar") }
            }
        }
    }

    // Quitar SI borra los archivos, al contrario que desinstalar. Por eso se
    // pregunta: es la unica accion de esta pantalla que no se puede deshacer.
    if (confirmarBorrado) {
        AlertDialog(
            onDismissRequest = { confirmarBorrado = false },
            title = { Text("Quitar ${juego.nombre}?") },
            text = {
                Text(
                    "Se borrara el paquete del telefono. Esto no se puede " +
                        "deshacer, aunque podras volver a conseguirlo en la Tienda."
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmarBorrado = false
                    vm.eliminarDeBiblioteca(juego.codigo)
                }) { Text("Quitar") }
            },
            dismissButton = {
                TextButton(onClick = { confirmarBorrado = false }) { Text("Cancelar") }
            },
        )
    }
}

// --- Cuenta ----------------------------------------------------------------

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun PestanaCuenta(
    email: String,
    estado: EstadoLudora,
    vm: VistaModeloLudora,
    actividad: FragmentActivity,
) {
    var confirmarBorrado by remember { mutableStateOf(false) }

    Column(Modifier.fillMaxSize()) {
        CenterAlignedTopAppBar(title = { Text("Cuenta") })

        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        ) {
            Text(email, style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(24.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))

            // --- biometria ---
            Text("Entrar con huella o cara", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(4.dp))
            Text(
                "Tu huella y tu cara NO se guardan: Android no se las da a " +
                    "ninguna aplicacion. Lo que se guarda es tu sesion, cifrada, " +
                    "y el lector del telefono es quien la desbloquea.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(12.dp))

            if (estado.biometria == Biometria.Disponibilidad.LISTA) {
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(if (estado.biometriaActiva) "Activado" else "Desactivado")
                    Switch(
                        checked = estado.biometriaActiva,
                        onCheckedChange = { quiere ->
                            if (quiere) vm.activarBiometria(actividad)
                            else vm.desactivarBiometria()
                        },
                        enabled = !estado.trabajando,
                    )
                }
            } else {
                Text(
                    textoBiometriaNoLista(estado.biometria),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.error,
                )
            }

            Spacer(Modifier.height(24.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))

            OutlinedButton(
                onClick = { vm.cerrarSesion() },
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Cerrar sesion") }

            Spacer(Modifier.height(8.dp))
            TextButton(
                onClick = { confirmarBorrado = true },
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text("Borrar la cuenta", color = MaterialTheme.colorScheme.error)
            }
        }
    }

    if (confirmarBorrado) {
        AlertDialog(
            onDismissRequest = { confirmarBorrado = false },
            title = { Text("Borrar la cuenta?") },
            text = {
                Text(
                    "Se borrara la cuenta de este telefono, con su biblioteca. " +
                        "Esto no se puede deshacer."
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmarBorrado = false
                    vm.borrarCuenta()
                }) { Text("Borrar") }
            },
            dismissButton = {
                TextButton(onClick = { confirmarBorrado = false }) { Text("Cancelar") }
            },
        )
    }
}

// ===========================================================================
// PIEZAS SUELTAS
// ===========================================================================

@Composable
private fun ColumnaFormulario(
    centrado: Boolean = false,
    contenido: @Composable () -> Unit,
) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(
            Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(24.dp),
            horizontalAlignment =
                if (centrado) Alignment.CenterHorizontally else Alignment.Start,
        ) { contenido() }
    }
}

@Composable
private fun CampoClave(valor: String, alCambiar: (String) -> Unit, etiqueta: String) {
    OutlinedTextField(
        value = valor,
        onValueChange = alCambiar,
        label = { Text(etiqueta) },
        singleLine = true,
        visualTransformation = PasswordVisualTransformation(),
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun Vacio(titulo: String, detalle: String) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(
            Modifier.padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                titulo,
                style = MaterialTheme.typography.titleMedium,
                textAlign = TextAlign.Center,
            )
            Spacer(Modifier.height(8.dp))
            Text(
                detalle,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
        }
    }
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
