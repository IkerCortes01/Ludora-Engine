package com.ludora.apk.datos

import android.content.Context
import com.ludora.apk.modelo.JuegoTienda
import java.io.File

/**
 * Catalogo de la Tienda, leido del propio telefono.
 *
 * ---------------------------------------------------------------------------
 * DE DONDE SALEN LOS JUEGOS HOY
 * ---------------------------------------------------------------------------
 * De un archivo `catalogo.txt` en el almacenamiento privado de la app, con el
 * mismo estilo clave=valor que el resto de Ludora:
 *
 *     juego=#ABCDEFGHIJ|Nombre del juego|0|104857600|4|90
 *     desc=Una linea de descripcion
 *     generos=#ACCION #PLATAFORMAS
 *
 * Los campos del `juego=` son, en orden: codigo, nombre, precio en centavos,
 * tamano en bytes, edad minima, edad maxima. `desc=` y `generos=` se aplican
 * al ultimo `juego=` leido.
 *
 * ---------------------------------------------------------------------------
 * POR QUE UN ARCHIVO Y NO UNA LISTA EN EL CODIGO
 * ---------------------------------------------------------------------------
 * Porque asi se puede probar la app con datos reales sin recompilarla: se
 * empuja un `catalogo.txt` al telefono con `adb push` y la Tienda lo ensena.
 * Una lista escrita en Kotlin obligaria a recompilar para cambiar un precio.
 *
 * Si el archivo NO existe, el catalogo esta VACIO y la Tienda lo dice con
 * claridad. No se inventan juegos de ejemplo: una tienda con juegos falsos que
 * no se pueden descargar es peor que una tienda vacia que explica por que lo
 * esta.
 *
 * El dia que exista servidor, esta clase se sustituye por su implementacion de
 * `ServidorLudora.catalogo()` y las pantallas no cambian.
 */
class CatalogoLocal(private val contexto: Context) {

    private val archivo: File
        get() = File(contexto.filesDir, "catalogo.txt")

    /** true si hay un catalogo que leer. */
    fun existe(): Boolean = archivo.exists()

    /**
     * Lee el catalogo. Devuelve lista vacia si no hay archivo.
     *
     * Las lineas que no se entienden se SALTAN en silencio en vez de abortar
     * la lectura entera: un juego mal escrito no debe vaciar la tienda.
     */
    fun cargar(): List<JuegoTienda> {
        if (!archivo.exists()) return emptyList()

        val juegos = mutableListOf<JuegoTienda>()
        var actual: JuegoTienda? = null

        try {
            archivo.forEachLine { linea ->
                val corte = linea.indexOf('=')
                if (corte <= 0) return@forEachLine

                val clave = linea.substring(0, corte).trim()
                val valor = linea.substring(corte + 1).trim()

                when (clave) {
                    "juego" -> {
                        // Al empezar uno nuevo se cierra el anterior.
                        actual?.let { juegos.add(it) }
                        actual = parsearJuego(valor)
                    }
                    "desc" -> actual = actual?.copy(descripcion = valor)
                    "generos" -> actual = actual?.copy(generos = valor)
                }
            }
            actual?.let { juegos.add(it) }
        } catch (e: Exception) {
            // Archivo ilegible: mejor tienda vacia que app cerrada.
            return emptyList()
        }

        return juegos
    }

    /** `codigo|nombre|precioCentavos|tamanoBytes|edadMin|edadMax` */
    private fun parsearJuego(valor: String): JuegoTienda? {
        val p = valor.split('|')
        if (p.size < 4) return null

        val codigo = p[0].trim()
        val nombre = p[1].trim()
        if (codigo.isBlank() || nombre.isBlank()) return null

        return JuegoTienda(
            codigo = codigo,
            nombre = nombre,
            descripcion = "",
            generos = "",
            precioCentavos = p[2].trim().toIntOrNull() ?: 0,
            tamanoBytes = p[3].trim().toLongOrNull() ?: 0L,
            edadMinima = p.getOrNull(4)?.trim()?.toIntOrNull() ?: 4,
            edadMaxima = p.getOrNull(5)?.trim()?.toIntOrNull() ?: 90,
        )
    }
}
