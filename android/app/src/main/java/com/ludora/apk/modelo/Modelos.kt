package com.ludora.apk.modelo

/**
 * Modelos de datos de Ludora en el telefono.
 *
 * ---------------------------------------------------------------------------
 * POR QUE ESTOS NOMBRES Y NO OTROS
 * ---------------------------------------------------------------------------
 * El Ludora de escritorio (C++ / Win32) guarda sus datos en archivos de texto
 * clave=valor. Este codigo es DISTINTO -- no se puede portar nada de Win32 a
 * Android -- pero los NOMBRES DE CAMPO se mantienen a proposito:
 *
 *     escritorio                  telefono
 *     ------------------------    --------------------------
 *     email=                      Cuenta.email
 *     salt=                       Cuenta.salt
 *     hash=                       Cuenta.hash
 *     dev=<salt>|<hash>|...       Cuenta.dispositivos
 *
 * El dia que exista un servidor que sincronice las dos cosas, la conversion
 * sera campo a campo y no una traduccion con sorpresas.
 *
 * Lo que NO se copia es la criptografia concreta: el escritorio deriva la
 * huella del equipo de MachineGuid + serial de volumen, cosas de Windows que
 * en Android no existen. Aqui el dispositivo se identifica de otra forma (ver
 * `seguridad/Dispositivo.kt`), pero el CAMPO sigue siendo un hash con su salt.
 */

/**
 * Un dispositivo verificado de una cuenta.
 *
 * Equivale a la linea `dev=<salt>|<hash>|<tipo>|<alta>|<ultimoAcceso>` del
 * archivo `<id>.cuenta` del escritorio.
 *
 * Igual que alli, la huella se guarda SIEMPRE hasheada: ni aqui ni en disco
 * aparece un identificador del telefono en claro.
 */
data class Dispositivo(
    /** Salt propio de esta huella (hex). */
    val salt: String,
    /** SHA-256(salt + identificadores del dispositivo), en hex. */
    val hash: String,
    /** Etiqueta generica: "Telefono", "Tableta". Nunca el modelo concreto. */
    val tipo: String,
    /** Alta, en segundos desde epoch. */
    val alta: Long,
    /** Ultimo acceso desde este dispositivo, en segundos desde epoch. */
    val ultimoAcceso: Long,
)

/**
 * Una cuenta de Ludora guardada en el telefono.
 *
 * A diferencia del escritorio, aqui se espera UNA sola cuenta en la practica:
 * un telefono es personal. Aun asi el almacen admite varias (ver
 * `datos/AlmacenCuentas.kt`) porque el formato ya lo permite y limitarlo seria
 * cerrarse una puerta gratis.
 */
data class Cuenta(
    val email: String,
    /** Salt de la contrasena (hex). */
    val salt: String,
    /** SHA-256(salt + contrasena), en hex. */
    val hash: String,
    val dispositivos: List<Dispositivo> = emptyList(),
) {
    /** Una cuenta sin alguno de los tres campos basicos esta corrupta. */
    val valida: Boolean
        get() = email.isNotBlank() && salt.isNotBlank() && hash.isNotBlank()
}

/**
 * Perfil publico: lo que otros verian de esta cuenta.
 *
 * Va aparte de `Cuenta` por lo mismo que en el escritorio: la cuenta es
 * secreto (hashes), el perfil es publico (nombre, descripcion). Mezclarlos
 * obligaria a leer los hashes cada vez que se quiere pintar un nombre.
 */
data class Perfil(
    val email: String,
    val nombreUsuario: String = "",
    val descripcion: String = "",
)

/**
 * Un juego tal y como lo muestra la Tienda.
 *
 * Es un reflejo reducido de `devcatalog::Game` del escritorio: aqui solo
 * estan los campos que una pantalla de telefono necesita pintar. Lo que falta
 * (historial de versiones, EULA por ambito, huella de revision) no se ha
 * olvidado: no cabe ni hace falta en una ficha de movil, y se anadira cuando
 * haya una pantalla que lo pida.
 */
data class JuegoTienda(
    /** Codigo "#" + caracteres. Es el identificador estable, como en escritorio. */
    val codigo: String,
    val nombre: String,
    val descripcion: String,
    /** Etiquetas tal y como se editan: "#ACCION #RPG". */
    val generos: String,
    /** Precio en centavos de dolar. 0 = gratis. Igual que `devcatalog::Price`. */
    val precioCentavos: Int,
    /** Tamano de la descarga en bytes, para avisar antes de bajar. */
    val tamanoBytes: Long,
    /** Franja de edad recomendada, como en el escritorio. */
    val edadMinima: Int = 4,
    val edadMaxima: Int = 90,
) {
    /** Precio ya formateado para pintar: "Gratis" o "12,99 USD". */
    val precioTexto: String
        get() = if (precioCentavos == 0) {
            "Gratis"
        } else {
            val enteros = precioCentavos / 100
            val centavos = precioCentavos % 100
            "%d,%02d USD".format(enteros, centavos)
        }
}

/**
 * Un juego ya en la biblioteca del usuario.
 *
 * Equivale a `library::Entry` del escritorio, adaptado: alli la entrada apunta
 * a un `.exe` o a un paquete `.tomate`; aqui no hay `.exe` que lanzar, asi que
 * se guarda la ruta del paquete descargado y su estado.
 */
data class JuegoBiblioteca(
    val codigo: String,
    val nombre: String,
    /** Ruta del paquete descargado dentro del almacenamiento privado de la app. */
    val rutaPaquete: String,
    val estado: EstadoJuego,
    /** Bytes ya descargados, para pintar la barra de progreso. */
    val bytesDescargados: Long = 0,
    val bytesTotales: Long = 0,
) {
    /** Progreso 0..1. Devuelve 0 si aun no se sabe el total. */
    val progreso: Float
        get() = if (bytesTotales <= 0) 0f
        else (bytesDescargados.toFloat() / bytesTotales.toFloat()).coerceIn(0f, 1f)
}

/**
 * En que punto esta un juego de la biblioteca.
 *
 * `DESINSTALADO` existe por la misma razon que en el escritorio: desinstalar
 * NO borra los archivos, bloquea el acceso. Asi reinstalar es inmediato y el
 * progreso de partida sigue intacto.
 */
enum class EstadoJuego {
    /** Comprado o adquirido, pero todavia sin bajar. */
    PENDIENTE,

    /** Bajando ahora mismo. */
    DESCARGANDO,

    /** Bajado y listo. */
    INSTALADO,

    /** Bloqueado por el usuario, con los archivos todavia en el telefono. */
    DESINSTALADO,

    /** La descarga fallo; se puede reintentar. */
    FALLIDO,
}
