"""El icono de Ludora para Android, sacado del de PC (Ludora icon.png).

Un icono adaptativo mide 108 dp y el telefono solo ensena el centro (un circulo
de unos 66 dp, segun la marca). La "L" se centra y se deja dentro de esa zona;
el resto es el mismo negro del icono de PC.
"""
import os
import sys
from PIL import Image

origen, res = sys.argv[1], sys.argv[2]
im = Image.open(origen).convert('RGB')
# La caja de la L: lo que no es el fondo negro.
gris = im.convert('L').point(lambda v: 255 if v > 40 else 0)
x0, y0, x1, y1 = gris.getbbox()
lado_l = max(x1 - x0, y1 - y0)
lado = int(round(lado_l / 0.47))            # la L ocupa el 47 % del lienzo
lienzo = Image.new('RGB', (lado, lado), im.getpixel((2, 2)))
cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
lienzo.paste(im, (lado // 2 - cx, lado // 2 - cy))
for nombre, px in (('mdpi', 108), ('hdpi', 162), ('xhdpi', 216), ('xxhdpi', 324), ('xxxhdpi', 432)):
    d = os.path.join(res, 'mipmap-' + nombre)
    os.makedirs(d, exist_ok=True)
    lienzo.resize((px, px), Image.LANCZOS).save(os.path.join(d, 'ic_launcher_foreground.png'), optimize=True)
lienzo.resize((432, 432), Image.LANCZOS).save(sys.argv[3])
print('caja', (x0, y0, x1, y1), 'lienzo', lado, 'fondo', im.getpixel((2, 2)))
