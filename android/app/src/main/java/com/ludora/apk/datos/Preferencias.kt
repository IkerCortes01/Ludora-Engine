package com.ludora.apk.datos

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

/**
 * Ajustes pequenos del usuario, con DataStore.
 *
 * DataStore y no SharedPreferences porque SharedPreferences lee del disco en
 * el hilo principal la primera vez que se toca, y eso es un tiron visible al
 * arrancar. DataStore es asincrono por diseno y devuelve `Flow`, que es
 * justo lo que Compose sabe observar.
 *
 * Aqui van SOLO ajustes: cosas pequenas que se pueden perder sin drama. Las
 * cuentas van en archivos (`AlmacenCuentas`) y la sesion va cifrada
 * (`SesionCifrada`). Guardar un secreto en DataStore seria un error: no esta
 * cifrado.
 */

/**
 * El DataStore de la app, uno solo.
 *
 * Como extension de `Context` porque es la forma que recomienda la libreria:
 * garantiza UNA sola instancia por proceso. Crear dos DataStore sobre el mismo
 * archivo lanza excepcion en tiempo de ejecucion.
 */
private val Context.almacenPreferencias: DataStore<Preferences> by preferencesDataStore(
    name = "ajustes_ludora"
)

class Preferencias(private val contexto: Context) {

    /**
     * Pestana en la que el usuario estaba la ultima vez.
     *
     * En el PRIMER arranque no hay valor guardado y se devuelve
     * `PESTANA_PRIMER_ARRANQUE` (la Tienda), que es lo que se pidio: alguien
     * que abre Ludora por primera vez no tiene biblioteca que mirar, asi que
     * lo que tiene sentido ensenarle es el catalogo.
     *
     * Despues se respeta lo ultimo que uso: volver siempre a la Tienda a quien
     * entra a jugar cada dia seria un paso de mas cada dia.
     */
    val ultimaPestana: Flow<Int> = contexto.almacenPreferencias.data.map { prefs ->
        prefs[CLAVE_ULTIMA_PESTANA] ?: PESTANA_PRIMER_ARRANQUE
    }

    suspend fun guardarUltimaPestana(indice: Int) {
        contexto.almacenPreferencias.edit { prefs ->
            prefs[CLAVE_ULTIMA_PESTANA] = indice
        }
    }

    /**
     * true si el usuario activo el desbloqueo con huella o cara.
     *
     * Es una preferencia aparte de "la clave existe en el keystore" porque son
     * dos cosas distintas: la clave puede existir y el usuario haber apagado
     * la opcion. Al encenderla se vuelve a pedir la contrasena.
     */
    val desbloqueoBiometrico: Flow<Boolean> =
        contexto.almacenPreferencias.data.map { prefs ->
            prefs[CLAVE_BIOMETRIA] ?: false
        }

    suspend fun guardarDesbloqueoBiometrico(activo: Boolean) {
        contexto.almacenPreferencias.edit { prefs ->
            prefs[CLAVE_BIOMETRIA] = activo
        }
    }

    /** Borra los ajustes. Se llama al cerrar sesion. */
    suspend fun limpiar() {
        contexto.almacenPreferencias.edit { it.clear() }
    }

    companion object {
        private val CLAVE_ULTIMA_PESTANA = intPreferencesKey("ultima_pestana")
        private val CLAVE_BIOMETRIA = booleanPreferencesKey("desbloqueo_biometrico")

        /**
         * Pestana del primer arranque: la Tienda.
         *
         * Coincide con `kStoreSection = 0` del `HomeScene.h` del escritorio, y
         * no por casualidad: las dos plataformas abren por el mismo sitio.
         */
        const val PESTANA_PRIMER_ARRANQUE = 0
    }
}
