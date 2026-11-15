package com.ludora.apk.datos

import android.content.Context
import com.ludora.apk.modelo.Cuenta
import com.ludora.apk.modelo.Dispositivo as DispositivoModelo
import com.ludora.apk.seguridad.Dispositivo
import com.ludora.apk.seguridad.Hash
import java.io.File

/**
 * Almacen de cuentas en el telefono.
 *
 * ===========================================================================
 * COMPATIBLE EN FORMATO CON EL `AccountStore` DEL ESCRITORIO
 * ===========================================================================
 * Mismo formato de archivo, a proposito, para que un futuro servidor pueda
 * sincronizar sin traducir nada:
 *
 *     cuentas/<id>.cuenta          (texto, clave=valor)
 *
 *         email=iker@ludora.engine
 *         salt=<hex>
 *         hash=<hex>
 *         dev=<salt>|<hash>|<tipo>|<alta>|<ultimoAcceso>
 *         dev=<salt>|<hash>|<tipo>|<alta>|<ultimoAcceso>
 *
 * Igual que en el escritorio:
 *   - UN archivo por cuenta, no uno con todas. Un archivo corrupto se lleva
 *     una cuenta por delante, no el almacen entero.
 *   - La clave `dev` se REPITE en vez de numerarse. Anadir o quitar un
 *     dispositivo es anadir o quitar una linea, y el parser sigue siendo
 *     trivial.
 *   - El `<id>` se deriva del correo y solo admite caracteres seguros, asi que
 *     un correo con `..` o barras no puede escaparse de la carpeta.
 *
 * ===========================================================================
 * EN QUE SE DIFERENCIA
 * ===========================================================================
 * El escritorio busca la carpeta junto al .exe para poder viajar en un USB.
 * En Android eso no tiene sentido ni esta permitido: la carpeta es siempre el
 * almacenamiento privado de la app (`filesDir`), al que ninguna otra app
 * accede. No hay "modo portatil" porque no hay donde ser portatil.
 */
class AlmacenCuentas(private val contexto: Context) {

    /** Carpeta `cuentas/`, creada al primer uso. */
    private val carpeta: File
        get() = File(contexto.filesDir, "cuentas").apply { mkdirs() }

    /**
     * Identificador de archivo derivado del correo.
     *
     * Minusculas, y solo letras, cifras, punto, guion y guion bajo. Todo lo
     * demas pasa a '_'. Esto es lo que impide que un correo malicioso como
     * `../../otra_cosa` escriba fuera de la carpeta.
     */
    fun idParaCorreo(email: String): String {
        val limpio = StringBuilder(email.length)
        for (c in email.lowercase()) {
            limpio.append(
                if (c.isLetterOrDigit() || c == '.' || c == '-' || c == '_') c else '_'
            )
        }
        return limpio.toString()
    }

    private fun archivoDe(email: String) = File(carpeta, "${idParaCorreo(email)}.cuenta")

    /** true si ya hay una cuenta con ese correo. */
    fun existe(email: String): Boolean = archivoDe(email).exists()

    /** Correos de todas las cuentas guardadas, ordenados. */
    fun listar(): List<String> =
        carpeta.listFiles { f -> f.extension == "cuenta" }
            ?.mapNotNull { leerArchivo(it)?.email }
            ?.sorted()
            ?: emptyList()

    /** Cuantas cuentas hay. */
    fun cuantas(): Int = carpeta.listFiles { f -> f.extension == "cuenta" }?.size ?: 0

    /**
     * Crea una cuenta nueva y vincula ESTE dispositivo.
     *
     * Igual que en el escritorio, solo falla si ya existe una cuenta con ESE
     * correo. Tener otras cuentas no impide crear esta.
     *
     * @return null si salio bien, o el mensaje de error para el usuario.
     */
    fun crear(email: String, contrasena: String): String? {
        val correo = email.trim()

        if (correo.isBlank()) return "Escribe tu correo."
        if (!correo.contains('@') || !correo.contains('.')) {
            return "Ese correo no parece valido."
        }
        if (contrasena.length < MINIMO_CONTRASENA) {
            return "La contrasena necesita al menos $MINIMO_CONTRASENA caracteres."
        }
        if (existe(correo)) return "Ya hay una cuenta con ese correo en este telefono."

        val salt = Hash.nuevoSalt()
        val cuenta = Cuenta(
            email = correo,
            salt = salt,
            hash = Hash.hashear(salt, contrasena),
            dispositivos = listOf(Dispositivo.entradaParaCuenta(contexto)),
        )

        return if (guardar(cuenta)) null else "No se pudo guardar la cuenta."
    }

    /**
     * Comprueba correo y contrasena.
     *
     * Si son correctas y este dispositivo aun no estaba en la lista, lo AÑADE
     * (sin desplazar a los demas), que es exactamente lo que hace el
     * escritorio.
     *
     * @return null si entro, o el mensaje de error.
     */
    fun verificar(email: String, contrasena: String): String? {
        val cuenta = leer(email)
            ?: return "No hay ninguna cuenta con ese correo en este telefono."

        val calculado = Hash.hashear(cuenta.salt, contrasena)
        if (!Hash.iguales(calculado, cuenta.hash)) {
            // Mensaje deliberadamente vago: decir "la contrasena esta mal"
            // confirma que el correo SI existe, y eso es informacion gratis
            // para quien este probando correos.
            return "El correo o la contrasena no cuadran."
        }

        val ahora = System.currentTimeMillis() / 1000
        val yaEsta = cuenta.dispositivos.any { Dispositivo.esEsteDispositivo(contexto, it) }

        val dispositivos = if (yaEsta) {
            // Solo se refresca el ultimo acceso del que ya estaba.
            cuenta.dispositivos.map {
                if (Dispositivo.esEsteDispositivo(contexto, it)) it.copy(ultimoAcceso = ahora)
                else it
            }
        } else {
            cuenta.dispositivos + Dispositivo.entradaParaCuenta(contexto)
        }

        guardar(cuenta.copy(dispositivos = dispositivos))
        return null
    }

    /** true si ESTE dispositivo esta entre los verificados de la cuenta. */
    fun dispositivoVerificado(email: String): Boolean {
        val cuenta = leer(email) ?: return false
        return cuenta.dispositivos.any { Dispositivo.esEsteDispositivo(contexto, it) }
    }

    /** Dispositivos de la cuenta, para pintarlos en la pantalla de Cuenta. */
    fun dispositivos(email: String): List<DispositivoModelo> =
        leer(email)?.dispositivos ?: emptyList()

    /**
     * Desvincula ESTE dispositivo. Los demas siguen verificados.
     *
     * @return null si salio bien, o el error.
     */
    fun olvidarEsteDispositivo(email: String): String? {
        val cuenta = leer(email) ?: return "Esa cuenta no esta en este telefono."
        val restantes = cuenta.dispositivos.filterNot {
            Dispositivo.esEsteDispositivo(contexto, it)
        }
        return if (guardar(cuenta.copy(dispositivos = restantes))) null
        else "No se pudo guardar el cambio."
    }

    /** Borra la cuenta del telefono. No toca el servidor (no hay servidor). */
    fun borrar(email: String): Boolean = archivoDe(email).delete()

    /** Marca que esta cuenta se acaba de usar desde este dispositivo. */
    fun tocar(email: String) {
        val cuenta = leer(email) ?: return
        val ahora = System.currentTimeMillis() / 1000
        guardar(
            cuenta.copy(
                dispositivos = cuenta.dispositivos.map {
                    if (Dispositivo.esEsteDispositivo(contexto, it)) it.copy(ultimoAcceso = ahora)
                    else it
                }
            )
        )
    }

    /** Lee una cuenta por correo, o null si no existe o esta corrupta. */
    fun leer(email: String): Cuenta? = leerArchivo(archivoDe(email))

    // -----------------------------------------------------------------------
    // Lectura y escritura del formato clave=valor
    // -----------------------------------------------------------------------

    private fun leerArchivo(f: File): Cuenta? {
        if (!f.exists()) return null
        return try {
            var email = ""
            var salt = ""
            var hash = ""
            val dispositivos = mutableListOf<DispositivoModelo>()

            f.forEachLine { linea ->
                val corte = linea.indexOf('=')
                if (corte > 0) {
                    val clave = linea.substring(0, corte).trim()
                    val valor = linea.substring(corte + 1).trim()
                    when (clave) {
                        "email" -> email = valor
                        "salt" -> salt = valor
                        "hash" -> hash = valor
                        // Las lineas `dev` se acumulan; el resto se ignora en
                        // silencio, para que una version futura que anada
                        // campos no rompa a esta.
                        "dev" -> parsearDispositivo(valor)?.let { dispositivos.add(it) }
                    }
                }
            }

            val cuenta = Cuenta(email, salt, hash, dispositivos)
            if (cuenta.valida) cuenta else null
        } catch (e: Exception) {
            null
        }
    }

    /** `<salt>|<hash>|<tipo>|<alta>|<ultimoAcceso>` */
    private fun parsearDispositivo(valor: String): DispositivoModelo? {
        val partes = valor.split('|')
        if (partes.size < 5) return null
        return DispositivoModelo(
            salt = partes[0],
            hash = partes[1],
            tipo = partes[2],
            alta = partes[3].toLongOrNull() ?: 0L,
            ultimoAcceso = partes[4].toLongOrNull() ?: 0L,
        )
    }

    private fun guardar(cuenta: Cuenta): Boolean {
        return try {
            val texto = buildString {
                append("email=").append(cuenta.email).append('\n')
                append("salt=").append(cuenta.salt).append('\n')
                append("hash=").append(cuenta.hash).append('\n')
                for (d in cuenta.dispositivos) {
                    append("dev=")
                        .append(d.salt).append('|')
                        .append(d.hash).append('|')
                        .append(d.tipo).append('|')
                        .append(d.alta).append('|')
                        .append(d.ultimoAcceso).append('\n')
                }
            }

            // Igual que con la sesion: temporal y renombrar. Un corte de luz a
            // mitad de la escritura no debe dejar una cuenta a medias, porque
            // una cuenta a medias es un usuario que no puede entrar.
            val destino = archivoDe(cuenta.email)
            val temporal = File(carpeta, "${idParaCorreo(cuenta.email)}.tmp")
            temporal.writeText(texto)
            if (destino.exists()) destino.delete()
            temporal.renameTo(destino)
        } catch (e: Exception) {
            false
        }
    }

    companion object {
        /**
         * Minimo de caracteres de contrasena.
         *
         * 8 es el suelo que recomienda el NIST. No se exigen mayusculas ni
         * simbolos a proposito: esas reglas empujan a la gente a
         * "Contrasena1!" y a apuntarla en un papel. La longitud es lo que de
         * verdad cuesta de romper.
         */
        const val MINIMO_CONTRASENA = 8
    }
}
