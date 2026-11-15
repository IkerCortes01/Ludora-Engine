package com.ludora.apk.seguridad

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.fragment.app.FragmentActivity
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import kotlin.coroutines.resume
import kotlinx.coroutines.suspendCancellableCoroutine

/**
 * Desbloqueo con huella o cara.
 *
 * ===========================================================================
 * LEE ESTO ANTES DE TOCAR NADA: QUE SE PUEDE Y QUE NO SE PUEDE HACER
 * ===========================================================================
 *
 * El encargo original decia "guardar los datos de la cara y de la huella".
 * ESO NO SE PUEDE HACER, y no por falta de ganas ni de permisos: Android no se
 * lo permite a NINGUNA aplicacion, ni a las del propio fabricante.
 *
 * Como funciona de verdad:
 *
 *   - La huella y la cara se registran en AJUSTES DEL SISTEMA, no en la app.
 *   - Las plantillas se guardan en un chip aparte (TEE / Secure Element), al
 *     que el sistema operativo entero -- no solo las apps -- tiene el acceso
 *     cortado.
 *   - Cuando la app pide autenticacion, el sistema ensena SU dialogo, compara
 *     por su cuenta, y a la app le devuelve UNA SOLA COSA: exito o fallo.
 *     Nunca una imagen, nunca una plantilla, nunca un identificador del dedo.
 *
 * Asi que una app que dijera "guardo tu huella" estaria mintiendo. Lo que si
 * se puede hacer -- y es lo que hace Ludora -- es esto:
 *
 *   1. La cuenta se crea UNA VEZ con correo y contrasena (eso es el secreto
 *      de verdad, y es lo que un dia viajara al servidor).
 *   2. La sesion abierta se guarda CIFRADA en el telefono.
 *   3. La clave que la descifra vive en el AndroidKeyStore (hardware), creada
 *      con `setUserAuthenticationRequired(true)`.
 *
 * El punto 3 es el que hace que esto no sea teatro: con esa bandera, el
 * keystore SE NIEGA a usar la clave hasta que el usuario se ha autenticado con
 * biometria. No es la app la que comprueba un booleano y decide; es el
 * hardware el que no suelta la clave. Si alguien parchea la app para saltarse
 * el `if`, sigue sin poder descifrar nada, porque la clave no sale.
 *
 * En resumen, y asi esta escrito tambien en el LEEME:
 *
 *     La biometria DESBLOQUEA una sesion ya existente.
 *     La biometria NO ES la credencial, y Ludora no guarda datos biometricos.
 *
 * ===========================================================================
 */
object Biometria {

    /** Nombre de la clave dentro del keystore. Una sola, para la sesion. */
    private const val ALIAS_CLAVE = "ludora_sesion_v1"

    private const val KEYSTORE = "AndroidKeyStore"
    private const val TRANSFORMACION = "AES/GCM/NoPadding"

    /** Bits del tag de autenticacion de GCM. 128 es el maximo y el recomendado. */
    private const val BITS_TAG = 128

    /**
     * Clase de biometria que se exige: SOLO la fuerte.
     *
     * Android distingue "fuerte" (BIOMETRIC_STRONG: huella y cara con
     * hardware serio) de "debil" (BIOMETRIC_WEAK: reconocimiento facial por
     * camara normal, enganable con una foto). Solo la fuerte puede proteger
     * una clave del keystore, asi que no es una eleccion de diseno: es la
     * unica que sirve para lo que hace falta aqui.
     */
    private const val CLASE_EXIGIDA = BiometricManager.Authenticators.BIOMETRIC_STRONG

    /** En que estado esta la biometria de este telefono. */
    enum class Disponibilidad {
        /** Hay sensor y hay al menos una huella o cara registrada. Se puede usar. */
        LISTA,

        /** Hay sensor, pero el usuario no ha registrado nada en Ajustes. */
        SIN_REGISTRAR,

        /** Este telefono no tiene sensor, o no es de clase fuerte. */
        SIN_SOPORTE,

        /** Hay sensor pero el sistema lo tiene ocupado o deshabilitado ahora. */
        NO_DISPONIBLE,
    }

    /**
     * Comprueba si se puede ofrecer el desbloqueo biometrico.
     *
     * Hay que llamar a esto ANTES de ensenar el boton: ofrecer huella en un
     * telefono sin sensor, o sin huellas registradas, solo produce un dialogo
     * que falla y un usuario confundido.
     */
    fun disponibilidad(contexto: android.content.Context): Disponibilidad {
        return when (BiometricManager.from(contexto).canAuthenticate(CLASE_EXIGIDA)) {
            BiometricManager.BIOMETRIC_SUCCESS ->
                Disponibilidad.LISTA

            BiometricManager.BIOMETRIC_ERROR_NONE_ENROLLED ->
                Disponibilidad.SIN_REGISTRAR

            BiometricManager.BIOMETRIC_ERROR_NO_HARDWARE,
            BiometricManager.BIOMETRIC_ERROR_SECURITY_UPDATE_REQUIRED ->
                Disponibilidad.SIN_SOPORTE

            else ->
                Disponibilidad.NO_DISPONIBLE
        }
    }

    /**
     * Crea la clave de sesion en el keystore del hardware.
     *
     * Se llama UNA vez, justo despues de crear la cuenta con contrasena.
     *
     * Las tres banderas que importan:
     *
     *   `setUserAuthenticationRequired(true)`
     *       El corazon del asunto. Sin autenticacion previa, el keystore no
     *       deja usar esta clave: `Cipher.init()` lanza excepcion.
     *
     *   `setInvalidatedByBiometricEnrollment(true)`
     *       Si alguien anade una huella nueva al telefono, esta clave se
     *       DESTRUYE y la sesion guardada deja de poder abrirse. Puede parecer
     *       molesto, y es justo lo que se quiere: si un tercero consigue
     *       registrar su dedo en el telefono, no debe heredar la sesion de
     *       Ludora. El usuario legitimo vuelve a entrar con su contrasena.
     *
     *   `setUserAuthenticationValidityDurationSeconds` -- NO se pone.
     *       Omitirlo significa "una autenticacion por uso de la clave". Ponerlo
     *       abriria una ventana de tiempo en la que la clave sirve sin volver a
     *       pedir el dedo, y para abrir una sesion no hace falta esa ventana.
     *
     * @return true si la clave quedo creada.
     */
    fun crearClaveSesion(): Boolean {
        return try {
            val generador = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
            val especificacion = KeyGenParameterSpec.Builder(
                ALIAS_CLAVE,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setUserAuthenticationRequired(true)
                .setInvalidatedByBiometricEnrollment(true)
                .build()

            generador.init(especificacion)
            generador.generateKey()
            true
        } catch (e: Exception) {
            // Falla en telefonos sin keystore con respaldo de hardware, o si el
            // usuario quito la biometria entre la comprobacion y esta llamada.
            // No se traga el error en silencio: quien llama decide que hacer.
            false
        }
    }

    /** true si la clave de sesion ya existe en el keystore. */
    fun existeClaveSesion(): Boolean {
        return try {
            val keystore = KeyStore.getInstance(KEYSTORE).apply { load(null) }
            keystore.containsAlias(ALIAS_CLAVE)
        } catch (e: Exception) {
            false
        }
    }

    /**
     * Borra la clave de sesion.
     *
     * Se llama al cerrar sesion. Borrar la clave inutiliza el archivo de
     * sesion cifrado de inmediato, sin tener que sobreescribirlo: sin clave,
     * esos bytes no son nada.
     */
    fun borrarClaveSesion() {
        try {
            val keystore = KeyStore.getInstance(KEYSTORE).apply { load(null) }
            if (keystore.containsAlias(ALIAS_CLAVE)) {
                keystore.deleteEntry(ALIAS_CLAVE)
            }
        } catch (e: Exception) {
            // Si no se puede borrar, la sesion sigue siendo inaccesible para
            // quien no pase la biometria, asi que no es un fallo critico.
        }
    }

    private fun claveSesion(): SecretKey? {
        return try {
            val keystore = KeyStore.getInstance(KEYSTORE).apply { load(null) }
            keystore.getKey(ALIAS_CLAVE, null) as? SecretKey
        } catch (e: Exception) {
            null
        }
    }

    /** Resultado de pedir la biometria. */
    sealed interface Resultado {
        /**
         * Autenticado. El `cipher` viene YA desbloqueado por el keystore y es
         * lo unico que sabe cifrar o descifrar la sesion.
         */
        data class Exito(val cipher: Cipher) : Resultado

        /** El usuario pulso "usar contrasena" o salio del dialogo. */
        data object Cancelado : Resultado

        /**
         * La clave ya no vale: tipicamente porque se registro una huella nueva
         * y `setInvalidatedByBiometricEnrollment` la destruyo. Hay que volver a
         * entrar con contrasena y crear una clave nueva.
         */
        data object ClaveInvalidada : Resultado

        /** Cualquier otro fallo; `mensaje` es lo que el sistema dijo. */
        data class Error(val mensaje: String) : Resultado
    }

    /**
     * Pide huella o cara para poder CIFRAR la sesion (al crear la cuenta).
     *
     * @param iv devuelve el vector de inicializacion que genero el cipher.
     *           Hay que guardarlo junto al texto cifrado: sin el no se puede
     *           descifrar. No es secreto.
     */
    suspend fun autenticarParaCifrar(actividad: FragmentActivity): Resultado {
        val clave = claveSesion() ?: return Resultado.ClaveInvalidada
        val cipher = try {
            Cipher.getInstance(TRANSFORMACION).apply { init(Cipher.ENCRYPT_MODE, clave) }
        } catch (e: android.security.keystore.KeyPermanentlyInvalidatedException) {
            return Resultado.ClaveInvalidada
        } catch (e: Exception) {
            return Resultado.Error(e.message ?: "No se pudo preparar el cifrado")
        }
        return mostrarDialogo(actividad, cipher)
    }

    /**
     * Pide huella o cara para DESCIFRAR la sesion guardada (al abrir la app).
     *
     * @param iv el vector que se guardo al cifrar.
     */
    suspend fun autenticarParaDescifrar(
        actividad: FragmentActivity,
        iv: ByteArray,
    ): Resultado {
        val clave = claveSesion() ?: return Resultado.ClaveInvalidada
        val cipher = try {
            Cipher.getInstance(TRANSFORMACION).apply {
                init(Cipher.DECRYPT_MODE, clave, GCMParameterSpec(BITS_TAG, iv))
            }
        } catch (e: android.security.keystore.KeyPermanentlyInvalidatedException) {
            return Resultado.ClaveInvalidada
        } catch (e: Exception) {
            return Resultado.Error(e.message ?: "No se pudo preparar el descifrado")
        }
        return mostrarDialogo(actividad, cipher)
    }

    /**
     * Ensena el dialogo del sistema y espera.
     *
     * El `cipher` se pasa dentro de un `CryptoObject`: eso es lo que ata la
     * autenticacion a ESTA operacion concreta. Sin el, el dialogo seria un
     * "di que si" decorativo que un atacante podria saltarse parcheando la
     * app; con el, es el keystore quien desbloquea el cipher, y la app no
     * tiene forma de fingirlo.
     */
    private suspend fun mostrarDialogo(
        actividad: FragmentActivity,
        cipher: Cipher,
    ): Resultado = suspendCancellableCoroutine { continuacion ->

        val info = BiometricPrompt.PromptInfo.Builder()
            .setTitle(actividad.getString(com.ludora.apk.R.string.biometria_titulo))
            .setSubtitle(actividad.getString(com.ludora.apk.R.string.biometria_subtitulo))
            // El boton de escape es obligatorio: si el sensor esta mojado o
            // sucio, el usuario tiene que poder entrar con su contrasena.
            .setNegativeButtonText(
                actividad.getString(com.ludora.apk.R.string.biometria_cancelar)
            )
            .setAllowedAuthenticators(CLASE_EXIGIDA)
            // Sin confirmacion extra: para desbloquear una sesion, el toque
            // del dedo ya es intencion suficiente.
            .setConfirmationRequired(false)
            .build()

        val prompt = BiometricPrompt(
            actividad,
            androidx.core.content.ContextCompat.getMainExecutor(actividad),
            object : BiometricPrompt.AuthenticationCallback() {

                override fun onAuthenticationSucceeded(
                    resultado: BiometricPrompt.AuthenticationResult,
                ) {
                    // El cipher que devuelve el resultado es el desbloqueado.
                    // Se usa ESE y no el de la variable local.
                    val desbloqueado = resultado.cryptoObject?.cipher
                    if (continuacion.isActive) {
                        continuacion.resume(
                            if (desbloqueado != null) Resultado.Exito(desbloqueado)
                            else Resultado.Error("El sistema no devolvio la clave")
                        )
                    }
                }

                override fun onAuthenticationError(codigo: Int, mensaje: CharSequence) {
                    if (!continuacion.isActive) return
                    val r = when (codigo) {
                        BiometricPrompt.ERROR_NEGATIVE_BUTTON,
                        BiometricPrompt.ERROR_USER_CANCELED,
                        BiometricPrompt.ERROR_CANCELED,
                        -> Resultado.Cancelado

                        else -> Resultado.Error(mensaje.toString())
                    }
                    continuacion.resume(r)
                }

                // onAuthenticationFailed (un dedo que no cuadra) NO se
                // propaga: el dialogo sigue abierto y el usuario reintenta.
                // Cerrar la pantalla al primer dedo mal leido seria hostil.
            },
        )

        prompt.authenticate(info, BiometricPrompt.CryptoObject(cipher))

        // Si la corrutina se cancela (el usuario sale de la pantalla), se
        // retira el dialogo en vez de dejarlo colgado sobre la app.
        continuacion.invokeOnCancellation { prompt.cancelAuthentication() }
    }
}
