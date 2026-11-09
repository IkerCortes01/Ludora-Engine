#pragma once

// Identificadores de recursos de Windows. Lo incluyen el .rc (que los define)
// y Window.cpp (que los carga), para que el numero viva en un solo sitio: si
// los dos lados dejan de coincidir, la ventana se queda sin icono y nada lo
// avisa en tiempo de compilacion.
//
// El 101 es el primer valor de la convencion habitual (por debajo de 100 estan
// los reservados). Ver la nota del .rc sobre por que el icono de la aplicacion
// debe conservar el ID mas bajo.
#define IDI_LUDORA 101
