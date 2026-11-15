package com.ludora.apk.datos

import android.content.Context
import com.ludora.apk.modelo.EstadoJuego
import com.ludora.apk.modelo.JuegoBiblioteca
import java.io.File

/**
 * Los juegos que el usuario tiene en ESTE telefono.
 *
 * Equivale al `library` del escritorio, y como alli es un registro APARTE del
 * catalogo: el catalogo son los juegos que se pueden conseguir, la biblioteca
 * son los que ya se tienen. Mezclarlos obligaria a marcar cada juego del
 * catalogo con "este si, este no" y a releer el catalogo entero para pintar la
 * biblioteca.
 *
 * Formato en `biblioteca.txt`, mismo estilo que el resto:
 *
 *     juego=#ABCDEFGHIJ|Nombre|INSTALADO|/ruta/al/paquete.tomate|0|104857600
 *
 * Campos: codigo, nombre, estado, ruta del paquete, bytes bajados, bytes
 * totales.
 */
class AlmacenBiblioteca(private val contexto: Context) {

    private val archivo: File
        get() = File(contexto.filesDir, "biblioteca.txt")

    /** Carpeta donde se bajan los paquetes de los juegos. */
    val carpetaJuegos: File
        get() = File(contexto.filesDir, "juegos").apply { mkdirs() }

    /** Todos los juegos de la biblioteca, en el orden en que se anadieron. */
    fun cargar(): List<JuegoBiblioteca> {
        if (!archivo.exists()) return emptyList()
        return try {
            archivo.readLines().mapNotNull { linea ->
                val corte = linea.indexOf('=')
                if (corte <= 0) return@mapNotNull null
                if (linea.substring(0, corte).trim() != "juego") return@mapNotNull null
                parsear(linea.substring(corte + 1).trim())
            }
        } catch (e: Exception) {
            emptyList()
        }
    }

    /** true si ese juego ya esta en la biblioteca. */
    fun tiene(codigo: String): Boolean = cargar().any { it.codigo == codigo }

    /**
     * Anade un juego, o actualiza el que ya estaba con el mismo codigo.
     *
     * Se identifica por CODIGO y no por nombre, igual que en el escritorio: un
     * nombre se renombra o cambia de mayusculas, un codigo no.
     */
    fun guardar(juego: JuegoBiblioteca): Boolean {
        val todos = cargar().toMutableList()
        val indice = todos.indexOfFirst { it.codigo == juego.codigo }
        if (indice >= 0) todos[indice] = juego else todos.add(juego)
        return escribir(todos)
    }

    /** Cambia solo el estado de un juego. */
    fun cambiarEstado(codigo: String, estado: EstadoJuego): Boolean {
        val todos = cargar().toMutableList()
        val indice = todos.indexOfFirst { it.codigo == codigo }
        if (indice < 0) return false
        todos[indice] = todos[indice].copy(estado = estado)
        return escribir(todos)
    }

    /**
     * Quita un juego del registro Y borra su paquete del telefono.
     *
     * Esto es DISTINTO de desinstalar: desinstalar deja los archivos y solo
     * bloquea el juego (`EstadoJuego.DESINSTALADO`), para que volver a
     * activarlo sea instantaneo. Esto de aqui si borra.
     */
    fun eliminar(codigo: String): Boolean {
        val todos = cargar()
        val juego = todos.find { it.codigo == codigo } ?: return false

        // Solo se borra dentro de la carpeta de juegos de la app. Una ruta
        // corrupta en el archivo no puede llevarse por delante otra cosa.
        val paquete = File(juego.rutaPaquete)
        if (paquete.exists() && paquete.absolutePath.startsWith(carpetaJuegos.absolutePath)) {
            paquete.delete()
        }

        return escribir(todos.filterNot { it.codigo == codigo })
    }

    private fun parsear(valor: String): JuegoBiblioteca? {
        val p = valor.split('|')
        if (p.size < 4) return null
        val codigo = p[0].trim()
        if (codigo.isBlank()) return null

        return JuegoBiblioteca(
            codigo = codigo,
            nombre = p[1].trim(),
            // Un estado desconocido (de una version futura) se lee como
            // PENDIENTE en vez de tirar la entrada: el juego sigue ahi.
            estado = runCatching { EstadoJuego.valueOf(p[2].trim()) }
                .getOrDefault(EstadoJuego.PENDIENTE),
            rutaPaquete = p[3].trim(),
            bytesDescargados = p.getOrNull(4)?.trim()?.toLongOrNull() ?: 0L,
            bytesTotales = p.getOrNull(5)?.trim()?.toLongOrNull() ?: 0L,
        )
    }

    private fun escribir(juegos: List<JuegoBiblioteca>): Boolean {
        return try {
            val texto = juegos.joinToString("\n") { j ->
                "juego=${j.codigo}|${j.nombre}|${j.estado.name}|${j.rutaPaquete}|" +
                    "${j.bytesDescargados}|${j.bytesTotales}"
            }
            val temporal = File(contexto.filesDir, "biblioteca.tmp")
            temporal.writeText(texto)
            if (archivo.exists()) archivo.delete()
            temporal.renameTo(archivo)
        } catch (e: Exception) {
            false
        }
    }
}
