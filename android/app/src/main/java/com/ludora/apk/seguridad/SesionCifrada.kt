package com.ludora.apk.seguridad

import android.content.Context
import java.io.File
import javax.crypto.Cipher

/**
 * La sesion abierta, guardada cifrada en el telefono.
 *
 * ---------------------------------------------------------------------------
 * QUE ES UNA "SESION" AQUI
 * ---------------------------------------------------------------------------
 * Hoy, sin servidor, una sesion es poco mas que el CORREO de la cuenta con la
 * que se entro. No hay token que guardar porque no hay nadie que lo emita.
 *
 * Aun asi se cifra, y no es paranoia decorativa: el dia que exista el servidor,
 * en este mismo archivo ira el token de acceso, y entonces el cifrado es
 * imprescindible. Montar el almacen cifrado ahora cuesta este archivo; montarlo
 * despues costaria una migracion con usuarios ya dentro.
 *
 * ---------------------------------------------------------------------------
 * FORMATO EN DISCO
 * ---------------------------------------------------------------------------
 * Un solo archivo, `sesion.bin`, en el almacenamiento privado de la app:
 *
 *     [1 byte ] longitud del IV (siempre 12 con AES-GCM)
 *     [N bytes] IV
 *     [resto  ] texto cifrado + tag de autenticacion de GCM
 *
 * El IV se guarda en claro porque NO es secreto: su trabajo es que dos
 * cifrados de lo mismo no den los mismos bytes. El tag de GCM va pegado al
 * final del texto cifrado (lo hace el propio `Cipher`), y es lo que detecta si
 * alguien manipulo el archivo: al descifrar, un byte cambiado lanza excepcion
 * en vez de devolver basura.
 */
object SesionCifrada {

    private const val ARCHIVO = "sesion.bin"

    private fun archivo(contexto: Context) = File(contexto.filesDir, ARCHIVO)

    /** true si hay una sesion guardada esperando a ser desbloqueada. */
    fun existe(contexto: Context): Boolean = archivo(contexto).exists()

    /**
     * Guarda el correo de la sesion, cifrado con el cipher que el keystore
     * acaba de desbloquear.
     *
     * El `cipher` tiene que venir de `Biometria.autenticarParaCifrar`: es el
     * unico que esta en modo ENCRYPT y autorizado. Pasarle otro no compila
     * mas, pero falla al usarlo, que es lo correcto.
     *
     * @return true si quedo guardada.
     */
    fun guardar(contexto: Context, cipher: Cipher, email: String): Boolean {
        return try {
            val cifrado = cipher.doFinal(email.toByteArray(Charsets.UTF_8))
            val iv = cipher.iv

            // Se escribe a un archivo temporal y se renombra al final. Si la
            // app muere a mitad de la escritura, el `sesion.bin` de antes sigue
            // entero en vez de quedar un archivo truncado que no se puede
            // descifrar -- y que dejaria al usuario fuera sin explicacion.
            val temporal = File(contexto.filesDir, "$ARCHIVO.tmp")
            temporal.outputStream().use { salida ->
                salida.write(iv.size)
                salida.write(iv)
                salida.write(cifrado)
            }
            temporal.renameTo(archivo(contexto))
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Lee el IV guardado, para poder pedir la biometria de descifrado.
     *
     * Hace falta en DOS pasos porque el orden lo exige: para construir el
     * cipher de descifrado hay que tener el IV, y para usarlo hay que haber
     * pasado la biometria. Asi que primero se lee el IV (no es secreto, no
     * necesita permiso), luego se autentica, y luego se descifra.
     *
     * @return el IV, o null si no hay sesion o el archivo esta corrupto.
     */
    fun leerIv(contexto: Context): ByteArray? {
        val f = archivo(contexto)
        if (!f.exists()) return null
        return try {
            f.inputStream().use { entrada ->
                val longitud = entrada.read()
                if (longitud <= 0 || longitud > 16) return null
                val iv = ByteArray(longitud)
                if (entrada.read(iv) != longitud) return null
                iv
            }
        } catch (e: Exception) {
            null
        }
    }

    /**
     * Descifra la sesion con el cipher ya desbloqueado por la biometria.
     *
     * @return el correo de la cuenta, o null si el archivo no cuadra. Un null
     *         aqui es sospechoso: significa que el archivo se manipulo o que
     *         se emparejo con un IV que no es el suyo.
     */
    fun abrir(contexto: Context, cipher: Cipher): String? {
        val f = archivo(contexto)
        if (!f.exists()) return null
        return try {
            val todo = f.readBytes()
            if (todo.isEmpty()) return null
            val longitudIv = todo[0].toInt() and 0xFF
            if (longitudIv <= 0 || todo.size <= 1 + longitudIv) return null

            val cifrado = todo.copyOfRange(1 + longitudIv, todo.size)
            String(cipher.doFinal(cifrado), Charsets.UTF_8)
        } catch (e: Exception) {
            // Incluye AEADBadTagException: el archivo fue alterado.
            null
        }
    }

    /**
     * Borra la sesion del disco.
     *
     * Se llama al cerrar sesion, junto con `Biometria.borrarClaveSesion()`.
     * Hacer las dos cosas es cinturon y tirantes: con la clave borrada el
     * archivo ya no sirve, pero tampoco hay razon para dejarlo ahi.
     */
    fun borrar(contexto: Context) {
        archivo(contexto).delete()
        File(contexto.filesDir, "$ARCHIVO.tmp").delete()
    }
}
