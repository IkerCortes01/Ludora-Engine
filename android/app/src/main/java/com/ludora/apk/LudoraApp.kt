package com.ludora.apk

import android.app.Application
import com.ludora.apk.datos.AlmacenBiblioteca
import com.ludora.apk.datos.AlmacenCuentas
import com.ludora.apk.datos.CatalogoLocal
import com.ludora.apk.datos.Preferencias
import com.ludora.apk.sync.PasarelaPagos
import com.ludora.apk.sync.PasarelaSinPagos

/**
 * La aplicacion, y de paso el contenedor de dependencias.
 *
 * ---------------------------------------------------------------------------
 * POR QUE NO HAY HILT NI KOIN
 * ---------------------------------------------------------------------------
 * Porque hay cinco dependencias, todas sin estado compartido complicado y
 * todas construibles con un `Context`. Una libreria de inyeccion aqui anadiria
 * anotaciones, generacion de codigo y tiempo de compilacion para resolver un
 * problema que se resuelve con cinco `by lazy`.
 *
 * Si el proyecto crece hasta tener varios modulos y dependencias con ciclos de
 * vida distintos, entonces si toca; hoy seria ceremonia.
 *
 * `by lazy` y no construccion directa para que abrir la app no cree de golpe
 * cosas que esa pantalla no necesita: quien entra a la Tienda no toca la
 * biblioteca.
 */
class LudoraApp : Application() {

    val preferencias: Preferencias by lazy { Preferencias(this) }
    val cuentas: AlmacenCuentas by lazy { AlmacenCuentas(this) }
    val catalogo: CatalogoLocal by lazy { CatalogoLocal(this) }
    val biblioteca: AlmacenBiblioteca by lazy { AlmacenBiblioteca(this) }

    /**
     * La pasarela de pagos en uso.
     *
     * Hoy es `PasarelaSinPagos`, que deja pasar los juegos gratis y dice la
     * verdad sobre los de pago. Es el UNICO punto que habria que cambiar
     * cuando exista Google Play Billing (ver `sync/ServidorLudora.kt`).
     */
    val pagos: PasarelaPagos by lazy { PasarelaSinPagos() }
}
