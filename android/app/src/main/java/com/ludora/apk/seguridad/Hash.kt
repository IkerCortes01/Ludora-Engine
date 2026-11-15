package com.ludora.apk.seguridad

import java.security.MessageDigest
import java.security.SecureRandom

/**
 * Hasheo de contrasenas, compatible EN FORMATO con el Ludora de escritorio.
 *
 * ---------------------------------------------------------------------------
 * QUE SE COPIA DEL ESCRITORIO Y QUE NO
 * ---------------------------------------------------------------------------
 * Se copia el ESQUEMA: SHA-256 sobre (salt + contrasena), con un salt
 * aleatorio por cuenta, y comparacion en tiempo constante. Asi los campos
 * `salt=` y `hash=` del archivo `<id>.cuenta` significan lo mismo en los dos
 * lados, y un futuro servidor no tiene que distinguir de donde viene cada
 * cuenta.
 *
 * ---------------------------------------------------------------------------
 * AVISO HONESTO SOBRE LA FUERZA DE ESTE ESQUEMA
 * ---------------------------------------------------------------------------
 * SHA-256 de una pasada es RAPIDO, y eso es malo para una contrasena: una
 * tarjeta grafica prueba miles de millones por segundo. Lo correcto para
 * contrasenas es un derivador lento (Argon2id, scrypt, PBKDF2 con muchas
 * iteraciones).
 *
 * Se mantiene SHA-256 aqui por una razon concreta y acotada: compatibilidad
 * con los archivos que el escritorio ya escribio. Cambiarlo ahora romperia
 * esos archivos.
 *
 * Lo que compensa el riesgo en el telefono es que el hash NO es la unica
 * defensa: el archivo de la cuenta vive en el almacenamiento privado de la
 * app, y la sesion que de verdad abre Ludora esta cifrada con una clave del
 * AndroidKeyStore que exige huella o cara (ver `Biometria.kt` y
 * `SesionCifrada.kt`). Un atacante con el archivo en la mano no entra solo con
 * romper el hash.
 *
 * TODO(seguridad): cuando exista el servidor y se pueda migrar en bloque, pasar
 * las dos plataformas a Argon2id. El escritorio YA tiene Argon2 implementado
 * (`src/app/Argon2.cpp`), pero lo usa para los paquetes .tomate, no para las
 * contrasenas de cuenta. La migracion es: al entrar con la contrasena correcta,
 * rehashear con Argon2id y marcar la cuenta como migrada.
 */
object Hash {

    /** Bytes de salt. 16 son los que usa el escritorio. */
    private const val BYTES_SALT = 16

    /**
     * Fuente de aleatoriedad del sistema.
     *
     * `SecureRandom()` sin argumentos usa el generador del sistema operativo,
     * que es lo que se quiere. No se le fija semilla: hacerlo solo puede
     * empeorarla.
     */
    private val aleatorio = SecureRandom()

    /** Un salt nuevo, en hex minusculas (el mismo formato que el escritorio). */
    fun nuevoSalt(): String {
        val bytes = ByteArray(BYTES_SALT)
        aleatorio.nextBytes(bytes)
        return aHex(bytes)
    }

    /**
     * SHA-256(salt + contrasena), en hex minusculas.
     *
     * El orden importa y es el del escritorio: PRIMERO el salt. Invertirlo
     * daria hashes distintos y las cuentas no cuadrarian entre plataformas.
     */
    fun hashear(salt: String, contrasena: String): String {
        val digest = MessageDigest.getInstance("SHA-256")
        digest.update(salt.toByteArray(Charsets.UTF_8))
        digest.update(contrasena.toByteArray(Charsets.UTF_8))
        return aHex(digest.digest())
    }

    /**
     * Compara dos hashes en TIEMPO CONSTANTE.
     *
     * Un `==` normal corta en cuanto encuentra el primer byte distinto, y
     * cuanto tarda en cortar filtra cuantos bytes acerto el atacante. Con
     * suficientes intentos cronometrados eso permite adivinar el hash byte a
     * byte. Recorrer siempre la cadena entera y acumular las diferencias con
     * un OR quita esa pista.
     */
    fun iguales(a: String, b: String): Boolean {
        if (a.length != b.length) return false
        var diferencia = 0
        for (i in a.indices) {
            diferencia = diferencia or (a[i].code xor b[i].code)
        }
        return diferencia == 0
    }

    /** Bytes a hex minusculas. */
    fun aHex(bytes: ByteArray): String {
        val sb = StringBuilder(bytes.size * 2)
        for (b in bytes) {
            val v = b.toInt() and 0xFF
            sb.append(HEX[v ushr 4])
            sb.append(HEX[v and 0x0F])
        }
        return sb.toString()
    }

    private val HEX = "0123456789abcdef".toCharArray()
}
