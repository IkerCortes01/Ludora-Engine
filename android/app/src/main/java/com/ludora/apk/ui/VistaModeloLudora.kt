package com.ludora.apk.ui

import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.ludora.apk.LudoraApp
import com.ludora.apk.datos.Preferencias
import com.ludora.apk.modelo.EstadoJuego
import com.ludora.apk.modelo.JuegoBiblioteca
import com.ludora.apk.modelo.JuegoTienda
import com.ludora.apk.seguridad.Biometria
import com.ludora.apk.seguridad.SesionCifrada
import com.ludora.apk.sync.PasarelaPagos
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/**
 * En que punto esta la app: antes de entrar, o ya dentro.
 *
 * Es un `sealed interface` y no un par de booleanos para que no puedan
 * existir estados imposibles. Con `estaDentro` y `pidiendoBiometria` sueltos
 * se podria llegar a "dentro y pidiendo biometria a la vez", que no significa
 * nada; aqui ese estado no se puede ni escribir.
 */
sealed interface Acceso {
    /** Arrancando: mirando si hay cuenta y sesion guardadas. */
    data object Comprobando : Acceso

    /** No hay ninguna cuenta en el telefono: toca crearla. */
    data object SinCuenta : Acceso

    /**
     * Hay cuenta y hay sesion cifrada: se puede desbloquear con huella o cara.
     * `email` se ensena para que el usuario sepa en que cuenta va a entrar.
     */
    data class Bloqueada(val email: String) : Acceso

    /** Hay cuenta pero no sesion que desbloquear: hace falta la contrasena. */
    data object PideContrasena : Acceso

    /** Dentro. */
    data class Dentro(val email: String) : Acceso
}

/**
 * Todo el estado de la interfaz, en un objeto.
 *
 * Un solo `StateFlow` con un `data class` dentro, en vez de diez flows
 * sueltos: asi una recomposicion ve siempre un estado coherente. Con flows
 * separados, Compose puede recomponer entre dos actualizaciones y pintar, por
 * ejemplo, la biblioteca de la cuenta vieja con el correo de la nueva.
 */
data class EstadoLudora(
    val acceso: Acceso = Acceso.Comprobando,

    /** Pestana visible. Los indices son los de `Pestana`. */
    val pestana: Int = Preferencias.PESTANA_PRIMER_ARRANQUE,

    val catalogo: List<JuegoTienda> = emptyList(),
    val biblioteca: List<JuegoBiblioteca> = emptyList(),

    /** Juego abierto en su ficha, o null si se esta en la lista. */
    val juegoAbierto: JuegoTienda? = null,

    /** Estado de la biometria en este telefono, para saber si ofrecerla. */
    val biometria: Biometria.Disponibilidad = Biometria.Disponibilidad.SIN_SOPORTE,

    /** true si el usuario tiene activado el desbloqueo biometrico. */
    val biometriaActiva: Boolean = false,

    /** Mensaje para el usuario (error o aviso). Se borra al leerlo. */
    val mensaje: String? = null,

    /** true mientras una operacion larga esta en marcha. */
    val trabajando: Boolean = false,
)

/**
 * Las seis secciones, las MISMAS del escritorio y con los mismos indices
 * (`kSectionNames` y `k...Section` de `HomeScene`):
 *
 *     0 TIENDA   1 CHAT   2 BIBLIOTECA   3 COMUNIDADES
 *     4 ESTADISTICAS & HORAS   5 CUENTA
 *
 * Antes el telefono tenia solo tres, compactadas. Se pidio que se viera igual
 * que en el PC, asi que estan las seis: las que alli estan vacias, aqui
 * tambien, y lo dicen. La franja se desliza de lado para que quepan.
 */
enum class Pestana(val indice: Int, val titulo: String) {
    TIENDA(0, "TIENDA"),
    CHAT(1, "CHAT"),
    BIBLIOTECA(2, "BIBLIOTECA"),
    COMUNIDADES(3, "COMUNIDADES"),
    ESTADISTICAS(4, "ESTADISTICAS & HORAS"),
    CUENTA(5, "CUENTA");

    companion object {
        fun de(indice: Int): Pestana = entries.firstOrNull { it.indice == indice } ?: TIENDA
    }
}

class VistaModeloLudora(private val app: LudoraApp) : ViewModel() {

    private val _estado = MutableStateFlow(EstadoLudora())
    val estado: StateFlow<EstadoLudora> = _estado.asStateFlow()

    init {
        arrancar()
    }

    /**
     * Decide por donde se entra, al abrir la app.
     *
     * El orden de las preguntas importa:
     *   1. Hay cuenta? Si no, no hay nada que desbloquear.
     *   2. Hay sesion cifrada Y biometria activa Y clave viva? Entonces se
     *      puede desbloquear con el dedo.
     *   3. Si no, contrasena.
     *
     * El punto 2 pide TRES cosas porque las tres pueden fallar por separado:
     * el usuario puede haber apagado la opcion, o haber registrado una huella
     * nueva (lo que destruye la clave), o haber borrado los datos de la app.
     */
    private fun arrancar() {
        viewModelScope.launch {
            val disponibilidad = Biometria.disponibilidad(app)
            val biometriaActiva = app.preferencias.desbloqueoBiometrico.first()
            val pestanaGuardada = app.preferencias.ultimaPestana.first()

            val correos = app.cuentas.listar()

            val acceso = when {
                correos.isEmpty() -> Acceso.SinCuenta

                biometriaActiva &&
                    SesionCifrada.existe(app) &&
                    Biometria.existeClaveSesion() &&
                    disponibilidad == Biometria.Disponibilidad.LISTA ->
                    Acceso.Bloqueada(correos.first())

                else -> Acceso.PideContrasena
            }

            _estado.value = _estado.value.copy(
                acceso = acceso,
                pestana = pestanaGuardada,
                biometria = disponibilidad,
                biometriaActiva = biometriaActiva,
            )
        }
    }

    // -----------------------------------------------------------------------
    // Crear cuenta y entrar
    // -----------------------------------------------------------------------

    /**
     * Crea la cuenta con correo y contrasena.
     *
     * Esto es lo que establece la credencial de verdad. La biometria se ofrece
     * DESPUES, y solo para desbloquear esta sesion: nunca sustituye a la
     * contrasena, porque la contrasena es lo unico que se puede recuperar en
     * otro telefono.
     */
    fun crearCuenta(email: String, contrasena: String, repetida: String) {
        viewModelScope.launch {
            if (contrasena != repetida) {
                avisar("Las dos contrasenas no son iguales.")
                return@launch
            }

            _estado.value = _estado.value.copy(trabajando = true)
            val error = app.cuentas.crear(email, contrasena)
            _estado.value = _estado.value.copy(trabajando = false)

            if (error != null) {
                avisar(error)
                return@launch
            }

            app.cuentas.tocar(email.trim())
            entrar(email.trim())
        }
    }

    /** Entra con correo y contrasena. */
    fun entrarConContrasena(email: String, contrasena: String) {
        viewModelScope.launch {
            _estado.value = _estado.value.copy(trabajando = true)
            val error = app.cuentas.verificar(email, contrasena)
            _estado.value = _estado.value.copy(trabajando = false)

            if (error != null) avisar(error) else entrar(email.trim())
        }
    }

    /**
     * Desbloquea la sesion guardada con huella o cara.
     *
     * Los pasos, y por que van en este orden:
     *   1. Leer el IV del archivo (no es secreto, no necesita autenticacion).
     *   2. Pedir la biometria, pasandole el IV para construir el cipher.
     *   3. Descifrar con el cipher que el KEYSTORE devolvio desbloqueado.
     *
     * Si el paso 3 devuelve null teniendo exito el paso 2, el archivo fue
     * manipulado: GCM lo detecta. En ese caso se cae a la contrasena en vez de
     * entrar.
     */
    fun desbloquearConBiometria(actividad: FragmentActivity) {
        viewModelScope.launch {
            val iv = SesionCifrada.leerIv(app)
            if (iv == null) {
                _estado.value = _estado.value.copy(acceso = Acceso.PideContrasena)
                return@launch
            }

            when (val r = Biometria.autenticarParaDescifrar(actividad, iv)) {
                is Biometria.Resultado.Exito -> {
                    val email = SesionCifrada.abrir(app, r.cipher)
                    if (email != null) {
                        app.cuentas.tocar(email)
                        entrar(email)
                    } else {
                        // Sesion ilegible: no se entra a medias.
                        SesionCifrada.borrar(app)
                        _estado.value = _estado.value.copy(acceso = Acceso.PideContrasena)
                        avisar("La sesion guardada no es valida. Entra con tu contrasena.")
                    }
                }

                Biometria.Resultado.Cancelado ->
                    _estado.value = _estado.value.copy(acceso = Acceso.PideContrasena)

                Biometria.Resultado.ClaveInvalidada -> {
                    // Tipicamente: se registro una huella nueva en el telefono.
                    // La clave se autodestruyo, que es lo que se queria.
                    limpiarDesbloqueo()
                    _estado.value = _estado.value.copy(acceso = Acceso.PideContrasena)
                    avisar(
                        "Se cambio la huella o la cara de este telefono, " +
                            "asi que hay que entrar con la contrasena."
                    )
                }

                is Biometria.Resultado.Error -> {
                    _estado.value = _estado.value.copy(acceso = Acceso.PideContrasena)
                    avisar(r.mensaje)
                }
            }
        }
    }

    /**
     * Activa el desbloqueo biometrico para la cuenta en la que se esta.
     *
     * Crea la clave en el keystore, pide el dedo una vez para autorizar el
     * cifrado, y guarda la sesion cifrada. A partir de aqui, abrir la app es
     * poner el dedo.
     */
    fun activarBiometria(actividad: FragmentActivity) {
        viewModelScope.launch {
            val acceso = _estado.value.acceso
            if (acceso !is Acceso.Dentro) {
                avisar("Entra en tu cuenta antes de activar el desbloqueo.")
                return@launch
            }

            if (_estado.value.biometria != Biometria.Disponibilidad.LISTA) {
                avisar(mensajeBiometriaNoLista(_estado.value.biometria))
                return@launch
            }

            // Se crea la clave de cero cada vez que se activa: si quedaba una
            // de antes, su sesion ya no vale.
            Biometria.borrarClaveSesion()
            if (!Biometria.crearClaveSesion()) {
                avisar("Este telefono no pudo crear la clave de seguridad.")
                return@launch
            }

            when (val r = Biometria.autenticarParaCifrar(actividad)) {
                is Biometria.Resultado.Exito -> {
                    if (SesionCifrada.guardar(app, r.cipher, acceso.email)) {
                        app.preferencias.guardarDesbloqueoBiometrico(true)
                        _estado.value = _estado.value.copy(biometriaActiva = true)
                        avisar("Listo: ya puedes entrar con tu huella o tu cara.")
                    } else {
                        limpiarDesbloqueo()
                        avisar("No se pudo guardar la sesion.")
                    }
                }

                Biometria.Resultado.Cancelado -> limpiarDesbloqueo()

                Biometria.Resultado.ClaveInvalidada -> {
                    limpiarDesbloqueo()
                    avisar("La clave de seguridad dejo de ser valida. Intentalo otra vez.")
                }

                is Biometria.Resultado.Error -> {
                    limpiarDesbloqueo()
                    avisar(r.mensaje)
                }
            }
        }
    }

    /** Apaga el desbloqueo biometrico y borra lo que lo sostiene. */
    fun desactivarBiometria() {
        viewModelScope.launch {
            limpiarDesbloqueo()
            avisar("Desbloqueo con huella desactivado.")
        }
    }

    private suspend fun limpiarDesbloqueo() {
        Biometria.borrarClaveSesion()
        SesionCifrada.borrar(app)
        app.preferencias.guardarDesbloqueoBiometrico(false)
        _estado.value = _estado.value.copy(biometriaActiva = false)
    }

    /** Cierra la sesion: borra sesion, clave y ajustes, y vuelve al inicio. */
    fun cerrarSesion() {
        viewModelScope.launch {
            limpiarDesbloqueo()
            _estado.value = EstadoLudora(
                acceso = if (app.cuentas.cuantas() > 0) Acceso.PideContrasena
                else Acceso.SinCuenta,
                biometria = _estado.value.biometria,
            )
        }
    }

    /**
     * Borra la cuenta de este telefono.
     *
     * No borra nada de ningun servidor, porque no hay servidor. Se dice asi en
     * la propia pantalla para que nadie crea que esto cancela algo remoto.
     */
    fun borrarCuenta() {
        viewModelScope.launch {
            val acceso = _estado.value.acceso
            if (acceso is Acceso.Dentro) app.cuentas.borrar(acceso.email)
            limpiarDesbloqueo()
            app.preferencias.limpiar()
            _estado.value = EstadoLudora(
                acceso = Acceso.SinCuenta,
                biometria = _estado.value.biometria,
            )
        }
    }

    /** Pasa a "dentro" y carga lo que las pantallas necesitan. */
    private fun entrar(email: String) {
        _estado.value = _estado.value.copy(
            acceso = Acceso.Dentro(email),
            catalogo = app.catalogo.cargar(),
            biblioteca = app.biblioteca.cargar(),
        )
    }

    // -----------------------------------------------------------------------
    // Navegacion
    // -----------------------------------------------------------------------

    /**
     * Cambia de pestana y lo recuerda.
     *
     * Se guarda en DataStore en cada cambio y no solo al salir: la app puede
     * morir sin avisar (el sistema la mata por memoria) y entonces no habria
     * ocasion de guardar nada.
     */
    fun irAPestana(indice: Int) {
        _estado.value = _estado.value.copy(pestana = indice, juegoAbierto = null)
        viewModelScope.launch { app.preferencias.guardarUltimaPestana(indice) }
    }

    fun abrirJuego(juego: JuegoTienda) {
        _estado.value = _estado.value.copy(juegoAbierto = juego)
    }

    fun cerrarJuego() {
        _estado.value = _estado.value.copy(juegoAbierto = null)
    }

    fun recargar() {
        _estado.value = _estado.value.copy(
            catalogo = app.catalogo.cargar(),
            biblioteca = app.biblioteca.cargar(),
        )
    }

    // -----------------------------------------------------------------------
    // Conseguir un juego
    // -----------------------------------------------------------------------

    /**
     * Anade un juego a la biblioteca.
     *
     * Flujo de TRES pasos, mas simple que el del escritorio (que tiene
     * formulario de precios, EULA por ambito y publicacion por fases):
     *
     *   1. Si ya esta, no se hace nada.
     *   2. Se pasa por la pasarela. Gratis -> adelante. De pago -> se dice la
     *      verdad: todavia no se puede.
     *   3. Queda en la biblioteca como PENDIENTE.
     *
     * NO se descarga nada: ver `TODO(descarga)` de mas abajo.
     */
    fun conseguirJuego(juego: JuegoTienda) {
        viewModelScope.launch {
            if (app.biblioteca.tiene(juego.codigo)) {
                avisar("Ya tienes ${juego.nombre} en tu biblioteca.")
                return@launch
            }

            _estado.value = _estado.value.copy(trabajando = true)
            val resultado = app.pagos.comprar(juego)
            _estado.value = _estado.value.copy(trabajando = false)

            when (resultado) {
                is PasarelaPagos.Resultado.Comprado -> {
                    app.biblioteca.guardar(
                        JuegoBiblioteca(
                            codigo = juego.codigo,
                            nombre = juego.nombre,
                            rutaPaquete = "",
                            estado = EstadoJuego.PENDIENTE,
                            bytesTotales = juego.tamanoBytes,
                        )
                    )
                    _estado.value = _estado.value.copy(
                        biblioteca = app.biblioteca.cargar(),
                        juegoAbierto = null,
                    )
                    avisar("${juego.nombre} esta en tu biblioteca.")
                }

                PasarelaPagos.Resultado.NoDisponibleTodavia -> avisar(
                    "Los juegos de pago todavia no se pueden comprar: " +
                        "falta la pasarela de Google Play. Los gratis si."
                )

                PasarelaPagos.Resultado.Cancelado -> Unit

                is PasarelaPagos.Resultado.Error -> avisar(resultado.mensaje)
            }
        }
    }

    /**
     * Desinstala un juego SIN borrar sus archivos.
     *
     * Misma decision que en el escritorio: se bloquea el acceso y se deja todo
     * en el telefono, asi reactivarlo es instantaneo y la partida guardada
     * sigue intacta.
     */
    fun desinstalar(codigo: String) {
        app.biblioteca.cambiarEstado(codigo, EstadoJuego.DESINSTALADO)
        _estado.value = _estado.value.copy(biblioteca = app.biblioteca.cargar())
    }

    /** Vuelve a activar un juego desinstalado. */
    fun reinstalar(codigo: String) {
        val juego = _estado.value.biblioteca.find { it.codigo == codigo } ?: return
        // Si el paquete nunca se bajo, vuelve a PENDIENTE, no a INSTALADO:
        // marcarlo instalado sin archivos seria mentir al usuario.
        val nuevo = if (juego.rutaPaquete.isBlank()) EstadoJuego.PENDIENTE
        else EstadoJuego.INSTALADO
        app.biblioteca.cambiarEstado(codigo, nuevo)
        _estado.value = _estado.value.copy(biblioteca = app.biblioteca.cargar())
    }

    /** Quita el juego y borra su paquete del telefono. */
    fun eliminarDeBiblioteca(codigo: String) {
        app.biblioteca.eliminar(codigo)
        _estado.value = _estado.value.copy(biblioteca = app.biblioteca.cargar())
    }

    /**
     * ======================================================================
     * SIN IMPLEMENTAR: DESCARGAR E INSTALAR EL JUEGO
     * ======================================================================
     *
     * Falta por dos motivos distintos, y conviene no confundirlos:
     *
     *   1. NO HAY DE DONDE BAJAR. Sin servidor no hay URL del paquete. Esto no
     *      es trabajo de codigo: es que no existe el origen.
     *
     *   2. NO HAY LECTOR DE PAQUETES .tomate EN ANDROID. El formato TMP3 del
     *      escritorio esta implementado sobre API de Windows: AES-GCM y
     *      Argon2id propios, y compresion con XPRESS_HUFF o LZNT1, que son
     *      compresores de Windows y NO EXISTEN en Android.
     *
     *      El punto 2 es el que tiene miga. Para leer un .tomate en el
     *      telefono hay que:
     *        - Reimplementar Argon2id (hay bibliotecas de Java que valen).
     *        - AES-GCM sale de serie en Java (`javax.crypto`), sin problema.
     *        - Y resolver la compresion: XPRESS_HUFF no tiene implementacion
     *          libre decente. Lo realista es que el escritorio empiece a
     *          escribir los paquetes destinados a movil con un compresor
     *          portatil (Deflate o Zstd), anotandolo en el campo `Codec` que
     *          el formato YA tiene reservado para eso.
     *
     * Lo que SI esta hecho y probado alrededor: el estado del juego en la
     * biblioteca, la barra de progreso, el borrado seguro del paquete y el
     * ciclo desinstalar/reinstalar. Cuando haya de donde bajar, esto se
     * enchufa en medio.
     *
     * TODO(descarga): implementar cuando existan (a) servidor con los paquetes
     * y (b) lector de .tomate para Android, en ese orden.
     */
    fun descargar(codigo: String) {
        avisar(
            "Todavia no se puede descargar: Ludora no tiene servidor del que " +
                "bajar los juegos. Mira el LEEME."
        )
    }

    // -----------------------------------------------------------------------
    // Mensajes
    // -----------------------------------------------------------------------

    private fun avisar(texto: String) {
        _estado.value = _estado.value.copy(mensaje = texto)
    }

    /** Algo que la pantalla ensena igual que el PC pero aqui aun no hace nada: se dice. */
    fun avisarPendiente(texto: String) = avisar(texto)

    /** La interfaz llama a esto cuando ya ensenio el mensaje. */
    fun mensajeLeido() {
        _estado.value = _estado.value.copy(mensaje = null)
    }

    private fun mensajeBiometriaNoLista(d: Biometria.Disponibilidad): String = when (d) {
        Biometria.Disponibilidad.SIN_REGISTRAR ->
            "Primero registra una huella o tu cara en los ajustes del telefono."
        Biometria.Disponibilidad.SIN_SOPORTE ->
            "Este telefono no tiene lector de huella ni cara compatible."
        Biometria.Disponibilidad.NO_DISPONIBLE ->
            "El lector no esta disponible ahora mismo. Intentalo mas tarde."
        Biometria.Disponibilidad.LISTA -> ""
    }

    /**
     * Fabrica del ViewModel.
     *
     * Hace falta porque el ViewModel recibe la `Application` por constructor,
     * y el `viewModel()` por defecto solo sabe construir los que no reciben
     * nada.
     */
    class Factory(private val app: LudoraApp) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            require(modelClass.isAssignableFrom(VistaModeloLudora::class.java)) {
                "Esta fabrica solo construye VistaModeloLudora"
            }
            return VistaModeloLudora(app) as T
        }
    }
}
