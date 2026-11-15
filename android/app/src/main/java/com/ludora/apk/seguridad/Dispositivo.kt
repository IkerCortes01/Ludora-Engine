package com.ludora.apk.seguridad

import android.content.Context
import java.io.File
import java.security.SecureRandom

/**
 * Huella de ESTE dispositivo, para la lista `dev=` de la cuenta.
 *
 * ---------------------------------------------------------------------------
 * POR QUE NO SE PARECE AL DEL ESCRITORIO
 * ---------------------------------------------------------------------------
 * El Ludora de escritorio deriva la huella del equipo de MachineGuid del
 * registro de Windows + el numero de serie del volumen. En Android no existe
 * ninguna de las dos cosas, y los identificadores equivalentes estan cerrados
 * a proposito desde Android 10:
 *
 *   - `ANDROID_ID` cambia por app y por usuario (no es malo, pero tampoco es
 *     del dispositivo).
 *   - IMEI y numero de serie exigen permisos de operador: Google rechaza la
 *     app de la tienda por pedirlos.
 *   - La direccion MAC devuelve un valor falso fijo desde Android 6.
 *
 * ---------------------------------------------------------------------------
 * LO QUE SE HACE EN SU LUGAR
 * ---------------------------------------------------------------------------
 * Un identificador ALEATORIO generado la primera vez que arranca la app y
 * guardado en su almacenamiento privado. Propiedades:
 *
 *   - Estable mientras la app este instalada: la cuenta reconoce el telefono.
 *   - Muere al desinstalar: desinstalar y volver a instalar cuenta como un
 *     dispositivo nuevo y pide contrasena. Es lo correcto -- no hay forma de
 *     saber si quien reinstala es el mismo.
 *   - No identifica a la persona fuera de Ludora: no se deriva de nada del
 *     hardware, asi que no sirve para rastrear entre apps.
 *
 * Igual que en el escritorio, lo que se guarda en la cuenta es el HASH con su
 * salt, nunca el identificador en claro.
 *
 * Tampoco se guarda ubicacion, ni nombre del telefono, ni del usuario: la
 * misma regla de privacidad que la cabecera de `AccountStore.h` del
 * escritorio.
 */
object Dispositivo {

    private const val ARCHIVO_ID = "id_dispositivo"

    /** Bytes del identificador. 32 bytes = 256 bits, de sobra para no repetir. */
    private const val BYTES_ID = 32

    /**
     * Identificador crudo de este dispositivo, creado la primera vez.
     *
     * Es `synchronized` porque dos pantallas que arranquen a la vez podrian
     * entrar aqui en paralelo la primera vez y generar dos identificadores,
     * quedandose el telefono con el ultimo que escriba. Pasa raras veces y
     * seria muy molesto de depurar.
     */
    @Synchronized
    private fun identificadorCrudo(contexto: Context): String {
        val f = File(contexto.filesDir, ARCHIVO_ID)
        if (f.exists()) {
            val guardado = runCatching { f.readText().trim() }.getOrNull()
            if (!guardado.isNullOrBlank()) return guardado
        }

        val bytes = ByteArray(BYTES_ID)
        SecureRandom().nextBytes(bytes)
        val nuevo = Hash.aHex(bytes)
        runCatching { f.writeText(nuevo) }
        return nuevo
    }

    /**
     * Crea la entrada `dev=` de este dispositivo para una cuenta.
     *
     * Cada cuenta recibe su propio salt para la misma huella. Asi dos cuentas
     * del mismo telefono guardan hashes DISTINTOS, y quien lea los dos
     * archivos no puede deducir que son el mismo aparato.
     */
    fun entradaParaCuenta(contexto: Context): com.ludora.apk.modelo.Dispositivo {
        val salt = Hash.nuevoSalt()
        val ahora = System.currentTimeMillis() / 1000
        return com.ludora.apk.modelo.Dispositivo(
            salt = salt,
            hash = Hash.hashear(salt, identificadorCrudo(contexto)),
            tipo = tipo(contexto),
            alta = ahora,
            ultimoAcceso = ahora,
        )
    }

    /**
     * true si esa entrada `dev=` corresponde a ESTE dispositivo.
     *
     * Se rehashea el identificador con el salt guardado y se compara. Es la
     * misma mecanica que verificar una contrasena.
     */
    fun esEsteDispositivo(
        contexto: Context,
        entrada: com.ludora.apk.modelo.Dispositivo,
    ): Boolean {
        val esperado = Hash.hashear(entrada.salt, identificadorCrudo(contexto))
        return Hash.iguales(esperado, entrada.hash)
    }

    /**
     * Etiqueta GENERICA del tipo de aparato, igual que el `kindName` del
     * escritorio ("Escritorio", "Portatil", "Tableta").
     *
     * Deliberadamente generica: decir "Telefono" no identifica a nadie, decir
     * el modelo exacto si ayudaria a rastrear. Se distingue tableta de
     * telefono por el ancho de pantalla en dp, que es como Android define esa
     * frontera.
     */
    private fun tipo(contexto: Context): String {
        val configuracion = contexto.resources.configuration
        val esTableta = configuracion.smallestScreenWidthDp >= 600
        return if (esTableta) "Tableta" else "Telefono"
    }

    /** Lo usa la pantalla de Cuenta para pintar la lista de dispositivos. */
    fun tipoVisible(contexto: Context): String = tipo(contexto)
}
