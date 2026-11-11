# Ludora Engine

Motor propio en **C++17 sobre Win32 puro**, con ventana **sin bordes (borderless)**
arrastrable y escalable, renderizado por software y salida a un `.exe` autónomo.

Sin frameworks, sin SDL/GLFW/Qt, sin runtime externo: solo la API de Windows.

- **Ejecutable:** ~172 KB
- **Memoria:** ~14 MB en ejecución
- **Rendimiento:** 60 FPS estables, ~20 % de un núcleo
- **Dependencias:** ninguna (solo DLLs del sistema: `user32`, `gdi32`, `winmm`)

---

## Compilar

Requiere **Visual Studio 2022** (con "Desarrollo para el escritorio con C++") y **CMake 3.20+**.

```bat
build.bat            :: compila Release
build.bat run        :: compila y ejecuta
build.bat test       :: ejecuta el autodiagnóstico
build.bat debug      :: compila Debug
build.bat clean      :: borra build/ y bin/
```

El resultado queda en `bin\Ludora.exe`. Es portátil: se puede copiar a cualquier
Windows 10/11 de 64 bits y ejecutar tal cual.

---

## Paleta

Negro dominante con el rojo como único acento. Todos los colores viven en un
bloque al principio de `src/app/DemoScene.cpp`, así que cambiar el tema es
tocar un solo sitio.

| Uso | Color |
|---|---|
| Lienzo | `#090909` |
| Barra superior | `#121212` |
| Rojo de marca / cerrar | `#E01E23` |
| Marco y separador | `#8E1116` |
| Hover (min/max) | `#3A0E10` |
| Pulsado (min/max) | `#5A1519` |
| Pulsado (cerrar) | `#9B1418` |
| Título | `#F2F2F2` |
| Iconos en reposo | `#B8B8B8` |

Con cualquier realce el icono pasa a blanco: sobre el rojo pleno de cerrar por
contraste, y sobre el rojo oscuro de los otros dos porque el gris se apagaría.

---

## Pantalla de acceso

Al arrancar aparece un formulario con **campo de correo**, **campo de
contraseña** debajo, y un botón de acción. Ambos campos son escribibles y
seleccionables (clic, arrastre, `Shift`+flechas, `Ctrl+A/C/X/V`).

El correo debe tener la forma `nombre@ludora.engine`. **La arroba se escribe
con `AltGr+Q`** en teclado español: AltGr llega a la aplicación como Ctrl+Alt,
así que el motor distingue ese caso de un `Ctrl` real — si no, la tecla
quedaría bloqueada como si fuera un atajo.

**Solo se permite una cuenta por equipo.** La primera vez el botón dice
`CREAR SESION`; una vez creada, la pantalla pasa a `INICIAR SESION` con el
correo precargado y ya no deja registrar otra. Al entrar correctamente se
salta a la pantalla principal, **vacía por ahora**.

La contraseña nunca se guarda: en `cuenta.dat` sólo queda
`SHA-256(salt + contraseña)` junto a un salt aleatorio de 16 bytes, generado
con la API criptográfica de Windows. La comparación es en tiempo constante y
los errores son genéricos ("Correo o contraseña incorrectos"), para no
revelar cuál de los dos falló.

## Dispositivo verificado

Al registrarte, el equipo queda **vinculado a la cuenta**: la próxima vez que
abras la aplicación entra sola, sin pedir la contraseña. Si entras con
contraseña en un equipo aún no vinculado, se vincula en ese momento — así
sólo la escribes una vez por dispositivo.

**Qué se guarda y qué no.** Esto importa, así que es explícito:

| Se guarda | No se guarda |
|---|---|
| Hash de la huella (SHA-256 + salt) | Ubicación (GPS, IP, zona horaria) |
| Tipo genérico: `Escritorio` / `Portatil` / `Tableta` | Nombre del equipo o del usuario |
| | Números de serie o identificadores en claro |

La huella se calcula a partir del `MachineGuid` de Windows y el número de
serie del volumen del sistema, pero **ese material nunca toca el disco**: se
combina con un salt aleatorio, se pasa por SHA-256 y sólo se guarda el
resultado. Del hash no se puede volver al dato de origen; sirve únicamente
para responder "¿es el mismo equipo, sí o no?".

El salt es **por cuenta**, no global. Dos instalaciones producen hashes
distintos para el mismo equipo, así que el valor guardado no funciona como
identificador rastreable entre aplicaciones.

La verificación del dispositivo **no sustituye a la contraseña**: sigue siendo
válida y sigue exigiéndose si el equipo no se reconoce.
`Account::forgetDevice()` desvincula el equipo — la cuenta y su contraseña
quedan intactas, y la próxima apertura vuelve a pedir credenciales.

Nada de esto sale del equipo: no hay red, todo queda en `cuenta.dat` local.

| Acción | Control |
|---|---|
| Cambiar de campo | `Tab`, o clic en el campo |
| Enviar | `Enter`, o el botón |
| Seleccionar texto | Clic y arrastre, `Shift`+flechas, `Ctrl+A` |
| Copiar / cortar / pegar | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` |
| Escribir la arroba | `AltGr+Q` |

## Nube Tomate — almacén cifrado

El botón **VERIFICAR DISPOSITIVO** pide tres claves (máx. 26 caracteres cada
una) y con ellas cifra un pequeño almacén local (`tomate.vault`) que vincula
este equipo a la cuenta. **No se puede pulsar GUARDAR Y ENTRAR sin verificar
antes** — el formulario avisa del paso que falta.

Cómo está cifrado, de dentro afuera:

1. **Argon2id** (RFC 9106) estira las tres claves una vez, con **64 MiB de
   memoria** y 3 pasadas. Es *memoria-dura*: obliga a un atacante a gastar esa
   memoria en **cada** intento de fuerza bruta, lo que arruina la ventaja de
   una GPU o un ASIC. Está implementado en el propio motor (`Argon2.cpp`)
   sobre un **BLAKE2b** propio (`Blake2b.cpp`), ambos verificados contra los
   vectores oficiales de sus RFC.
2. La salida de Argon2 alimenta una capa de **AES-256-GCM** con su clave
   PBKDF2-HMAC-SHA512 (120 000 iteraciones), su salt y su nonce propios. El tag
   de autenticación detecta cualquier manipulación antes de usar los datos.

   *Fueron 10 capas encadenadas, y se bajó a una a propósito:* las diez
   derivaban del **mismo** secreto, así que quien rompiera una rompía todas —no
   multiplicaban la seguridad, solo el trabajo. Lo que de verdad encarece un
   ataque es el estirado (Argon2id + PBKDF2), que no se ha tocado. Lo que sí
   costaban era tiempo: diez pasadas de AES sobre el archivo entero, cada una
   con una copia completa del buffer. Al **abrir** se sigue admitiendo cualquier
   número de capas —la cuenta va escrita en la cabecera—, así que los almacenes
   de 10 capas ya guardados se leen igual.

Qué **no** hace, dicho sin rodeos:

- **No hay puerta trasera.** No existe clave maestra, salt fijo ni nonce
  reutilizado. Nadie —tampoco quien tenga el código— puede abrir el almacén
  sin las tres claves correctas.
- **No sube nada a ningún servidor.** "Nube Tomate" es, por ahora, solo la
  parte de dispositivo. El cifrado ocurre *antes* de que los datos salgan del
  equipo, de modo que un servidor futuro solo vería bytes ilegibles (extremo a
  extremo). La sincronización de red no está implementada: requiere
  infraestructura propia.
- **El almacén guarda el tipo de dispositivo** (`Escritorio`/`Portatil`/…),
  **nunca ubicación ni IP.**

El código está comentado así —diciendo también lo que *no* aporta cada
decisión— para que nadie asuma una fortaleza que no tiene.

### Paquetes de juego (`.tomate` v3)

Lo que produce **GUARDAR ARCHIVOS Y ACTUALIZAR** no es un vault del equipo: es
un paquete **distribuible**, que tiene que abrirse en el PC de quien lo
descargue. Por eso se sella con un secreto de distribución y no con la huella
del autor — sellarlo con su huella daría un juego que solo él puede instalar.

El cifrado aquí no esconde nada del jugador (va a ejecutar el juego de todos
modos): sirve para que el paquete **viaje como una unidad íntegra** y no se
pueda alterar a medias sin que se detecte.

Cómo está hecho (`TomatePack.{h,cpp}`):

1. El juego se trata como un **flujo continuo** de archivos concatenados, y ese
   flujo se corta en **bloques de 8 MiB**. Los bloques no se alinean con los
   archivos a propósito: un juego de miles de archivos pequeños daría miles de
   bloques diminutos, que comprimen mucho peor.
2. **Argon2id se paga UNA vez** por paquete. De esa clave maestra sale la
   subclave de cada bloque con **BLAKE2b con clave**, que es casi gratis.
   Estirar por bloque no añadiría seguridad —el secreto es el mismo— y costaría
   minutos.
3. En la derivación de cada subclave entran el **número de bloque** y un
   **identificador aleatorio del paquete**. Así un bloque solo descifra en *su*
   posición de *su* paquete: reordenarlos, duplicarlos o traer uno de otro
   paquete hace fallar el tag.
4. Cada bloque lleva su **propio tag AES-256-GCM**, así que una alteración se
   detecta al abrir ese bloque, sin descifrar el paquete entero.
5. El **índice va cifrado** igual que los datos: los nombres de los archivos
   del juego no se leen abriendo el `.tomate` con un editor.
6. Al extraer, **toda ruta se valida antes de escribir nada**. Una entrada con
   `..\..\Windows\System32\...` se rechaza: instalar un juego no puede escribir
   fuera de su carpeta.

Por qué se cambió el formato anterior, con números medidos en este equipo:

| | antes (todo en memoria) | ahora (en flujo) |
|---|---|---|
| guardar 400 MB | 14,9 s | **3,1 s** |
| memoria al guardar 400 MB | 1087 MB | **218 MB** |
| memoria al guardar 1,2 GB | *no cabía: el proceso moría* | **217 MB** |
| tamaño del paquete | 63,5 % | **59,5 %** |

El formato viejo construía el paquete entero en RAM **tres veces** (empaquetado,
comprimido y cifrado), así que necesitaba ~2,2 veces el tamaño del juego en
memoria libre y tenía un tope de 4 GB. De ahí venía el aviso de "no hay memoria
suficiente", que a un autor con un juego grande no le dejaba salida. Ahora la
memoria es **fija** —unas decenas de MB— sea el juego de 10 MB o de 100 GB, y
no hay tope de tamaño.

La compresión elige por bloque. Medido sobre 150 MB de mezcla real de juego:

| codec | ratio | comprimir | descomprimir |
|---|---|---|---|
| LZNT1 (el que se usaba) | 57,8 % | 43,8 MB/s | 99,5 MB/s |
| XPRESS | 50,6 % | 183,0 MB/s | 665,5 MB/s |
| XPRESS_HUFF | 42,3 % | 89,6 MB/s | 116,1 MB/s |

`XPRESS_HUFF` deja el paquete más pequeño, pero descomprime 6 veces más lento,
y eso no es simétrico: comprimir lo hace el autor **una** vez, descomprimir lo
hacen **todos** los jugadores en **cada** instalación. Así que se cata un trozo
pequeño primero; si no comprime —el caso de texturas y audio, el grueso de un
juego— el bloque se guarda crudo, que es lo más rápido posible. Si comprime
bien, se compara y solo se paga la descompresión lenta cuando el ahorro es
real. Los bloques se comprimen y cifran **en paralelo**, un hilo por núcleo
menos uno (la interfaz sigue dibujándose mientras se guarda).

Los paquetes del formato anterior **siguen abriéndose**: el lector mira la
marca del archivo y usa el código viejo si hace falta.

### Sesión recordada

Tras iniciar sesión una vez en un equipo, la app **no vuelve a pedir
credenciales** allí: al abrir, entra directa a la pantalla principal. La
sesión se guarda en `sesion.tomate`, cifrada con el mismo Vault y con un
secreto **ligado a este equipo** (la huella de hardware, un hash que nunca se
almacena en claro). Por eso el token no sirve copiado a otra máquina: al
abrirlo, la huella no coincide y el descifrado falla.

En la sección **Cuenta** hay un botón **CERRAR SESIÓN**: borra el token, y la
próxima apertura vuelve a pedir credenciales. La cuenta y el perfil no se
tocan.

## Controles de ventana

| Acción | Control |
|---|---|
| Minimizar / Maximizar / Cerrar | Botones de la esquina superior derecha |
| Mover la ventana | Arrastrar la barra superior |
| Redimensionar | Arrastrar bordes y esquinas |
| Maximizar / restaurar | Doble clic en la barra |
| Ajustar a los lados | Aero Snap (arrastrar a un borde) |
| Salir | `ESC` o Alt+F4 |

Los botones tienen realce al pasar el ratón (rojo en cerrar, como Windows) y
estado *pulsado*. Si pulsas un botón y sueltas fuera de él, la acción se
cancela — igual que en cualquier ventana nativa.

---

## Cómo funciona la ventana sin bordes

Una ventana borderless que se comporte bien es más sutil que quitar `WS_CAPTION`.
El motor lo resuelve con dos mensajes:

**`WM_NCCALCSIZE`** — devolver `0` cuando `wParam == TRUE` elimina por completo el
marco no-cliente, de modo que el área cliente ocupa el 100 % de la ventana. Se
conserva `WS_THICKFRAME`, que es lo que mantiene vivo el redimensionado del
gestor de ventanas.

**`WM_NCHITTEST`** — se le indica a Windows qué zona simula cada punto:
`HTCAPTION` en la franja superior (arrastre), `HTLEFT` / `HTBOTTOMRIGHT` / etc.
en los bordes (resize). Windows hace el trabajo pesado, así que **Aero Snap,
el ajuste a bordes y el doble clic para maximizar siguen funcionando gratis** —
cosa que se pierde al implementar el arrastre a mano con `WM_MOUSEMOVE`.

### Los botones de ventana

Hay una trampa: si toda la barra devuelve `HTCAPTION`, Windows se queda los
clics y los botones nunca reciben `WM_LBUTTONDOWN`. Por eso `Window` expone el
callback **`isInteractiveArea`**: la escena declara qué zonas son botones y el
hit-test devuelve `HTCLIENT` justo ahí. El resto de la barra sigue arrastrando.

Dos detalles que hacen que se sienta nativo:

- **`WM_MOUSELEAVE`** (vía `TrackMouseEvent`) — sin esto el resalte de un botón
  se queda "pegado" cuando el cursor sale de la ventana sin más eventos.
- **`WM_GETMINMAXINFO`** acota el maximizado al área de trabajo del monitor: una
  `WS_POPUP` sin marco se maximizaría a pantalla completa, tapando la barra de tareas.

---

## Escala del contenido

El motor separa dos resoluciones:

```
resolución LÓGICA          resolución FÍSICA
(framebuffer, donde   -->  (área cliente,
 dibuja la escena)          lo que se ve)
        └────── StretchDIBits ──────┘
```

`escala = 2.0` → el framebuffer es la mitad de ancho y alto; cada píxel lógico
ocupa 2×2 físicos (efecto zoom, estética pixel art).
`escala = 0.5` → el framebuffer es el doble; más detalle en la misma ventana.

La escena nunca se entera: sigue dibujando contra `fb.width()` / `fb.height()`.
Con `S` se alterna entre escalado nítido (*nearest*) y suavizado (*halftone*).

La ventana es **PerMonitor-V2 DPI aware**, así que al arrastrarla a un monitor
con otra escala de Windows se readapta sin quedar borrosa.

---

## Estructura

```
src/
├─ main.cpp              punto de entrada, configuración del motor
├─ core/
│  ├─ Types.h            Vec2, Color, Size, Recti
│  ├─ Clock.{h,cpp}      reloj de alta precisión, delta acotado, FPS
│  ├─ Scene.h            interfaz de contenido (update/render)
│  └─ Engine.{h,cpp}     bucle principal, limitador de FPS
├─ platform/
│  ├─ Window.{h,cpp}     ventana borderless, hit-test, DPI  ← núcleo
│  └─ Input.{h,cpp}      teclado y ratón con detección de flanco
├─ render/
│  ├─ Framebuffer.{h,cpp}  superficie BGRA + primitivas de dibujo
│  └─ RendererGDI.{h,cpp}  presentación y escalado sin parpadeo
└─ app/
   ├─ DemoScene.{h,cpp}  pantalla de acceso (formulario)
   ├─ HomeScene.{h,cpp}  pantalla tras iniciar sesión (vacía por ahora)
   ├─ Account.{h,cpp}    cuenta única: validación, SHA-256 + salt
   ├─ Device.{h,cpp}     huella del equipo (hash) y tipo genérico
   ├─ Vault.{h,cpp}      almacén cifrado: Argon2id + AES-256-GCM
   ├─ TomatePack.{h,cpp} paquetes de juego en flujo: bloques, paralelo
   ├─ Argon2.{h,cpp}     Argon2id (RFC 9106), memoria-dura
   ├─ Blake2b.{h,cpp}    BLAKE2b (RFC 7693), base de Argon2
   ├─ Session.{h,cpp}    sesión recordada, cifrada y ligada al equipo
   ├─ Font5x7.{h,cpp}    fuente bitmap embebida (tabla generada)
   ├─ SelfTest.{h,cpp}   autodiagnóstico (--selftest)
   └─ Screenshot.{h,cpp} volcado del diseño a .bmp (--screenshot)
ui/
├─ Theme.h              paleta única del motor
├─ TitleBar.{h,cpp}     barra y botones de ventana (compartida)
└─ TextField.{h,cpp}    campo de texto: cursor, selección, portapapeles
```

---

## Autodiagnóstico

`Ludora.exe --selftest` ejercita el motor desde dentro de su propio bucle y
escribe `selftest-report.txt`. Verifica desde el proceso mismo a propósito:
un `SetWindowPos` desde otro proceso bloquea hasta que el hilo dueño de la
ventana atiende el mensaje.

`Ludora.exe --screenshot` vuelca el diseño a `shot-*.bmp` en tres estados
(reposo, hover en cerrar, hover en maximizar) leyendo el framebuffer. También
por fiabilidad: capturar la pantalla falla en cuanto otra ventana se pone
delante, mientras que el framebuffer es siempre exactamente lo que se dibuja.

Estado actual: **92/92 pruebas superadas** — estilos borderless, resize,
límites mínimos, escala (2.0x / 0.5x / recorte al máximo), opacidad, topmost,
framebuffer, ritmo del bucle, métrica de la fuente, geometría de los botones,
zonas de arrastre, maximizado acotado a la barra de tareas, inyección de
eventos de ratón y **verificación del hover leyendo el píxel dibujado**
(`0xFF121212` en reposo → `0xFFE01E23` con el ratón sobre cerrar).

También el sistema de cuentas: formato del correo (dominio, arroba única,
caracteres válidos), rechazo de una segunda cuenta, verificación de
credenciales, **que la contraseña no aparezca en texto plano**, y que dos
cuentas con la misma contraseña produzcan hashes distintos — lo que demuestra
que el salt es aleatorio y no reutilizado. El test respalda y restaura
`cuenta.dat` si existe, así que no destruye la cuenta real.

Y el vínculo con el dispositivo, incluida la parte de privacidad: que el
`MachineGuid`, el nombre del equipo y el del usuario **no aparezcan** en el
archivo, que sólo existan los seis campos previstos, que el tipo sea una
categoría genérica, que la huella sea estable en el mismo equipo pero cambie
al cambiar el salt, y el ciclo completo registrarse → cerrar → reabrir
entrando sin contraseña.

---

## Añadir contenido propio

Implementa `Scene` y pásala a `Engine::run`:

```cpp
class MiJuego : public ludora::Scene {
    void update(ludora::Engine& e, double dt) override {
        if (e.input().keyPressed(VK_SPACE)) { /* ... */ }
    }
    void render(ludora::Engine&, ludora::Framebuffer& fb) override {
        fb.clear(ludora::Color::fromHex(0x101820));
        fb.fillCircle(100, 100, 20, ludora::Color::fromHex(0xFF7043));
    }
};

// en main.cpp
return engine.run(std::make_unique<MiJuego>());
```

`Framebuffer` ofrece `clear`, `fillRect`, `drawRect`, `drawLine`, `fillCircle`,
`blendPixel` (con mezcla alpha) y `setPixel`. Todo con recorte a los límites.

---

## Siguiente paso natural

El renderizado está aislado tras `RendererGDI`. Para saltar a GPU, se extrae una
interfaz `IRenderer` con `resizeTarget` / `present` y se añade una implementación
Direct3D 11 — `Engine`, `Window` y las escenas no cambian.
