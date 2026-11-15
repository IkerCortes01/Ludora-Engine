# Ludora para telefono (Android)

Version de Ludora para Android. Es un **proyecto nuevo**, no una conversion
del Ludora de escritorio: ese esta escrito en C++17 sobre Win32 y GDI puros
(`HWND`, `BitBlt`, framebuffer de software), y nada de eso existe en Android.
Aqui el codigo es Kotlin + Jetpack Compose, distinto por completo.

Lo que SI se conserva son los **conceptos y los nombres de los campos** de los
datos, para que el dia que haya servidor la conversion sea campo a campo y no
una traduccion con sorpresas.

---

## Estado al 15 nov 2026: compila

Se compilo por primera vez con JDK 17, Gradle 8.9 y el SDK de Android 35
(`gradle assembleDebug`), sin errores y sin tocar el codigo. Sale un APK de
pruebas de unos 18 MB (`com.ludora.apk.debug`, firma de depuracion), que es el
que ofrece la pagina de Ludora. **Sigue sin probarse en un telefono.** Lo de
abajo se escribio antes de esa compilacion y se conserva como historia.

## Lo primero (nota original): esto todavia no se puede compilar aqui

**En este equipo no hay con que compilarlo.** Lo comprobe: no hay SDK de
Android, ni Gradle, ni `adb`, ni `kotlinc`. Solo un runtime de Java 8, que no
sirve (hace falta 17).

Para compilarlo necesitas:

1. **Android Studio** (trae el SDK, Gradle y el JDK 17).
2. Abrir esta carpeta con `File > Open`.
3. Android Studio descargara las dependencias y generara el *wrapper* de
   Gradle (`gradlew`), que no esta incluido.
4. `Run` con un telefono conectado o un emulador.

Como no se ha podido compilar, **el codigo no esta probado en un telefono**.
Esta escrito para ser correcto a la vista y las firmas estan revisadas una a
una contra el resto del proyecto, pero la primera compilacion puede sacar
algun detalle.

---

## Lo que pediste y lo que de verdad se puede hacer

### Rostro y huella: se hace, pero no como suena

Pediste que la app pida el rostro, lo guarde, luego la huella, y que con eso
se pueda entrar. **Guardar el rostro o la huella es imposible**, y no por una
limitacion de esta app: Android no entrega esos datos a ninguna aplicacion,
nunca. El lector es del sistema y lo unico que devuelve es un **si o un no**.
Los datos biometricos no salen del hardware seguro del telefono.

Asi que esta hecho de la forma correcta, que es como funcionan los bancos:

1. La cuenta se crea **una vez** con correo y contrasena.
2. Esa sesion se guarda **cifrada**, con una clave que vive en el
   `AndroidKeyStore` y que esta marcada con `setUserAuthenticationRequired`.
3. A partir de ahi, la huella o la cara **desbloquean esa clave**, y con ella
   la sesion. Se entra sin escribir nada.

Consecuencia importante: **sigue haciendo falta una contrasena la primera
vez**. Con biometria sola no hay cuenta que abrir, porque no hay nada que
identifique a nadie: el telefono solo sabe decir "si, es el dueno de este
telefono".

Y un detalle que esta contemplado en el codigo: si registras una huella nueva
en el telefono, Android **invalida la clave** a proposito (es una defensa
contra que alguien anada su dedo al telefono de otro). En ese caso la app lo
detecta (`ClaveInvalidada`) y pide la contrasena otra vez. Por eso el boton de
"entrar con la contrasena" esta siempre a la vista: si el sensor falla, nadie
se queda fuera de su propia cuenta.

### "Conectada al sistema": hoy no hay sistema al que conectarse

Busque codigo de red en el Ludora de escritorio. **No hay ninguno**: ni HTTP,
ni sockets. Las cuentas, la biblioteca y las compras son archivos en el disco
local del PC.

Asi que hoy esta app es **independiente**: sus datos viven en el telefono y no
se sincronizan con el PC, porque no hay servidor en medio que los lleve de uno
a otro. En `sync/ServidorLudora.kt` esta la interfaz preparada con lo que
haria falta, sin inventarse una URL que no existe.

> Nota posterior: en el repositorio de la web (Voxel World) hay un
> `deploy/api/servidor.py` que **no estaba en la copia local** cuando se
> escribio esto. Si ese servidor es el camino, la sincronizacion deberia
> apuntar ahi. Conviene revisarlo antes de implementar nada.

---

## Que funciona, que es un hueco

### Hecho

- **Proyecto Gradle completo**: `settings.gradle.kts`, catalogo de versiones,
  manifiesto, recursos e icono.
- **Cuentas en el telefono** (`datos/AlmacenCuentas.kt`): mismo formato de
  campos que el `AccountStore` del escritorio (`email`, `salt`, `hash`,
  `dev=`), con SHA-256 y salt por cuenta.
- **Biometria de verdad** (`seguridad/Biometria.kt`): `BiometricPrompt`, clave
  en `AndroidKeyStore`, y los cuatro estados del lector (lista, sin registrar,
  sin soporte, no disponible) tratados por separado.
- **Sesion cifrada** (`seguridad/SesionCifrada.kt`).
- **Las tres pestanas**: Tienda, Biblioteca y Cuenta.
- **Arranca en la Tienda la primera vez**, y despues **vuelve a la pestana
  donde estabas**, guardada en DataStore. Es el mismo comportamiento que se
  acaba de anadir al Ludora de escritorio.
- **Ciclo de la biblioteca**: desinstalar sin borrar archivos (como en el
  escritorio), reinstalar, y quitar (que si borra, y por eso pregunta antes).

### Hueco, y por que

| Que | Por que no esta |
|---|---|
| **Descargar juegos** | No hay de donde bajarlos (sin servidor). Y ademas falta un lector de `.tomate` para Android: el formato TMP3 usa compresion XPRESS_HUFF/LZNT1, que son de Windows y no existen en Android. Lo realista es que el escritorio empiece a escribir los paquetes de movil con Deflate o Zstd, usando el campo `Codec` que el formato ya tiene reservado. |
| **Comprar de pago** | Hace falta Google Play Billing, que necesita cuenta de desarrollador publicada. Los juegos **gratis** si se pueden conseguir. |
| **Sincronizar con el PC** | No hay servidor (ver arriba). |
| **Chat** | El escritorio tiene `kChatSection`. Sin servidor no hay con quien hablar. |
| **Catalogo real** | `datos/CatalogoLocal.kt` lee lo que haya en local; sin servidor, la Tienda sale vacia y lo dice. |

Ninguno de estos huecos esta fingido: donde no se puede hacer algo, la app lo
**dice** en pantalla en vez de simular que funciona.

---

## Estructura

```
app/src/main/java/com/ludora/apk/
  MainActivity.kt          una sola Activity (FragmentActivity, lo exige BiometricPrompt)
  LudoraApp.kt             Application: crea los almacenes
  modelo/Modelos.kt        Cuenta, Dispositivo, JuegoTienda, JuegoBiblioteca
  datos/                   cuentas, biblioteca, catalogo y preferencias (DataStore)
  seguridad/               biometria, sesion cifrada, hash, huella del dispositivo
  sync/ServidorLudora.kt   interfaz del servidor que aun no existe
  ui/VistaModeloLudora.kt  todo el estado, en un StateFlow
  ui/PantallaRaiz.kt       las pantallas
  ui/tema/Tema.kt          colores de Ludora (azul y violeta del escritorio)
```

---

## Si retomas esto

El orden que tiene sentido:

1. **Decidir el servidor.** Todo lo demas cuelga de eso. Mirar primero el
   `deploy/api/servidor.py` que ya existe en el repositorio de la web.
2. **Compresion portatil en los paquetes del escritorio** (campo `Codec`), que
   es lo que desbloquea poder leer `.tomate` en el telefono.
3. Y ya entonces, descargas y pagos.
