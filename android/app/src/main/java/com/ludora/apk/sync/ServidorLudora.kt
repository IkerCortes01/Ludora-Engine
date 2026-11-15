package com.ludora.apk.sync

import com.ludora.apk.modelo.JuegoTienda

/**
 * ===========================================================================
 * INTERFAZ SIN IMPLEMENTAR: NO HAY SERVIDOR DE LUDORA
 * ===========================================================================
 *
 * Esto es un contrato vacio a proposito. Antes de rellenarlo hay que saber por
 * que esta vacio.
 *
 * El Ludora de escritorio NO TIENE RED. Ni una linea. Se reviso el codigo
 * entero buscando HTTP, WinHttp, sockets y InternetOpen: la unica coincidencia
 * era una bandera de camara de Media Foundation. Las cuentas, el catalogo de
 * juegos y las compras son archivos en el disco local del usuario.
 *
 * Consecuencia directa: NO EXISTE NINGUNA DIRECCION A LA QUE CONECTARSE. No
 * hay API, no hay dominio, no hay protocolo definido.
 *
 * Por eso aqui no hay una URL inventada ni un cliente HTTP a medias. Un
 * `BASE_URL = "https://api.ludora.com"` apuntando a la nada habria sido peor
 * que este archivo: daria la impresion de que el trabajo esta medio hecho,
 * cuando lo que falta es la decision de que protocolo hablar.
 *
 * ---------------------------------------------------------------------------
 * QUE HACE FALTA ANTES DE IMPLEMENTAR ESTO
 * ---------------------------------------------------------------------------
 * Por orden, y ninguno de los pasos es trabajo de esta app:
 *
 *   1. Decidir donde vive el servidor y quien lo paga.
 *   2. Definir el protocolo. Lo natural es exponer los formatos que ya
 *      existen: las cuentas como `<id>.cuenta`, el catalogo como los juegos
 *      publicados, los paquetes como `.tomate` (TMP3).
 *   3. Decidir la autenticacion. Hoy la cuenta es un SHA-256 con salt en un
 *      archivo local; para un servidor hace falta emitir tokens y, antes de
 *      eso, pasar las contrasenas a Argon2id (ver el aviso de `Hash.kt`).
 *   4. Hacer que el escritorio hable tambien, o nadie tendra con quien
 *      sincronizar.
 *
 * Mientras eso no exista, Ludora en el telefono funciona SOLO, con lo que
 * tenga guardado el propio telefono. Es una limitacion real, no un bug.
 *
 * TODO(servidor): implementar cuando los cuatro puntos de arriba esten
 * resueltos. El resto de la app ya esta escrita contra esta interfaz, asi que
 * enchufarla no deberia tocar ninguna pantalla.
 */
interface ServidorLudora {

    /**
     * Catalogo de la Tienda.
     *
     * Hoy lo sirve `CatalogoLocal`, que lee lo que haya en el telefono.
     */
    suspend fun catalogo(): Result<List<JuegoTienda>>

    /**
     * Comprueba las credenciales contra el servidor y devuelve un token.
     *
     * Hoy esto lo hace `AlmacenCuentas.verificar()` en local. La diferencia
     * importante: en local, la cuenta solo existe en ESTE telefono. Con
     * servidor, la misma cuenta serviria en el PC y en el movil, que es lo que
     * el usuario espera de una plataforma de juegos.
     */
    suspend fun iniciarSesion(email: String, contrasena: String): Result<String>

    /**
     * Sube los cambios locales de la biblioteca.
     *
     * Es lo que haria que comprar un juego en el PC lo hiciera aparecer en el
     * telefono.
     */
    suspend fun sincronizarBiblioteca(token: String): Result<Unit>
}

/**
 * ===========================================================================
 * INTERFAZ SIN IMPLEMENTAR: LAS COMPRAS DE VERDAD
 * ===========================================================================
 *
 * Cobrar dinero en Android no es cuestion de escribir codigo. Hace falta:
 *
 *   - Una cuenta de desarrollador de Google Play (de pago, y con revision).
 *   - Google Play Billing: Google EXIGE su pasarela para contenido digital y
 *     se queda su comision. Cobrar por fuera es motivo de expulsion de la
 *     tienda.
 *   - Dar de alta cada juego como producto en la consola de Play, uno a uno.
 *   - Un servidor que verifique los recibos. Sin esa verificacion, cualquiera
 *     con un telefono rooteado se regala todos los juegos.
 *
 * El ultimo punto es el que lo bloquea del todo: la verificacion de compras
 * NO SE PUEDE hacer solo en el telefono, y no hay servidor.
 *
 * Asi que no hay aqui ninguna implementacion de pago falsa. Lo que SI funciona
 * en esta version es adquirir juegos GRATIS (`precioCentavos == 0`), que no
 * necesita pasarela ni servidor y deja el flujo de descarga e instalacion
 * completo y probado. Cuando haya pasarela, los juegos de pago entran por el
 * mismo camino.
 *
 * TODO(pagos): implementar con Google Play Billing cuando exista cuenta de
 * desarrollador Y servidor de verificacion de recibos.
 */
interface PasarelaPagos {

    /** Resultado de intentar comprar. */
    sealed interface Resultado {
        data class Comprado(val codigoJuego: String) : Resultado
        data object Cancelado : Resultado
        data class Error(val mensaje: String) : Resultado

        /**
         * Lo unico que devuelve la implementacion de hoy para un juego de
         * pago, y es la respuesta honesta.
         */
        data object NoDisponibleTodavia : Resultado
    }

    suspend fun comprar(juego: JuegoTienda): Resultado
}

/**
 * La pasarela que esta en uso ahora mismo.
 *
 * No finge nada: a los juegos gratis les dice que si (porque de verdad no hay
 * nada que cobrar) y a los de pago les dice que todavia no se puede. Ninguna
 * de las dos respuestas es mentira.
 */
class PasarelaSinPagos : PasarelaPagos {

    override suspend fun comprar(juego: JuegoTienda): PasarelaPagos.Resultado {
        return if (juego.precioCentavos == 0) {
            PasarelaPagos.Resultado.Comprado(juego.codigo)
        } else {
            PasarelaPagos.Resultado.NoDisponibleTodavia
        }
    }
}
