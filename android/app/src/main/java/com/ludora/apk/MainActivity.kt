package com.ludora.apk

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.getValue
import androidx.compose.runtime.collectAsState
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ludora.apk.ui.PantallaRaiz
import com.ludora.apk.ui.VistaModeloLudora
import com.ludora.apk.ui.tema.TemaLudora

/**
 * La unica Activity de la app.
 *
 * ---------------------------------------------------------------------------
 * POR QUE HEREDA DE FragmentActivity Y NO DE ComponentActivity
 * ---------------------------------------------------------------------------
 * Por BiometricPrompt. El dialogo del sistema se monta internamente como un
 * Fragment, asi que exige un `FragmentActivity`. Con `ComponentActivity`
 * compila igual pero revienta en tiempo de ejecucion al abrir el dialogo, con
 * un error que no dice nada de esto.
 *
 * `FragmentActivity` extiende `ComponentActivity`, asi que `setContent` y todo
 * Compose siguen funcionando igual. No se pierde nada.
 *
 * ---------------------------------------------------------------------------
 * UNA SOLA ACTIVITY
 * ---------------------------------------------------------------------------
 * Toda la navegacion (desbloqueo, pestanas, fichas de juego) es estado dentro
 * de Compose. Varias Activities obligarian a pasar datos por Intents y a
 * reconstruir el estado en cada salto.
 */
class MainActivity : FragmentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Dibuja detras de la barra de estado y de la de navegacion. Es lo
        // normal en Android moderno; los Scaffold de dentro ya respetan los
        // margenes para que nada quede tapado.
        enableEdgeToEdge()

        setContent {
            TemaLudora {
                val vm: VistaModeloLudora = viewModel(
                    factory = VistaModeloLudora.Factory(application as LudoraApp)
                )
                val estado by vm.estado.collectAsState()

                PantallaRaiz(
                    estado = estado,
                    vm = vm,
                    // La pantalla de desbloqueo necesita la Activity para
                    // poder ensenar el dialogo del sistema. Se le pasa esta,
                    // que es la unica que hay.
                    actividad = this,
                )
            }
        }
    }
}
