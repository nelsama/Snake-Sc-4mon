# Editor de cara — diseño y formato de `FACE.DAT`

La cabeza del gusano no es un dibujo fijo: se diseña **dentro del juego**, se
guarda en la SD y se recupera en la siguiente partida.

---

## 1. Cómo se entra y se usa

Desde el banner: **`CREAR CARA`**.

| Control | Acción |
|---|---|
| stick / WASD | mover el cursor (con autorepeat) |
| fuego / `espacio` | pintar la celda, o cambiar el color del cuerpo |
| `G` | guardar en la SD y salir |
| `Q` | cancelar y salir |

En pantalla hay dos zonas y el cursor se mueve **libremente entre las dos**:

```
        ┌──────────────┐
        │  lienzo 8×8  │      ┌  CUERPO ┐
        │   (64×64 px) │      │  bloque │
        └──────────────┘      └─────────┘
        COLOR: 1
```

- En la **última columna** del lienzo, «derecha» **sale** al recuadro del cuerpo.
- Dentro del recuadro, «izquierda» **vuelve** al lienzo, a la misma fila.

---

## 2. Los cuatro «colores»

El lienzo no tiene una paleta libre: el dibujo se guarda como **índices** y estos
se traducen al patrón del sprite. Solo hay 2 bits por píxel (4 valores), pero
hacen falta cinco estados, así que el transparente ocupa un índice propio:

| Índice | Significado | En el patrón |
|---|---|---|
| **0** | transparente (no pinta) | color 0 |
| **1** | cuerpo | color 1 |
| **2** | pupila | color 2 |
| **4** | ojo | color 3 |
| 3 | *libre* | — |

`FC_COLOR(v)` hace la conversión: el 4 (ojo) pasa a 3 y el resto se deja igual.
El salto del 3 es deliberado: permite numerar como ve el usuario (0/1/2/4) sin
gastar un byte más en `FACE.DAT`.

> ⚠️ En el **fondo** el color 0 es siempre transparente (`BG_COLOR`), así que el
> lienzo no puede pintar con el índice 0 de una paleta. Por eso el azul, el
> amarillo y el blanco del lienzo van en los colores **1, 2 y 3**.

---

## 3. De un glifo a cuatro direcciones

Se dibuja **una sola cara, mirando a la derecha**. Las otras tres salen solas:

| Dirección | Cómo |
|---|---|
| derecha | patrón tal cual (`SPR_PAT_HEAD_R`) |
| arriba | girado **90° en software** (`SPR_PAT_HEAD_U`) |
| izquierda | derecha con `VC_SPR_FLIP_X` |
| abajo | derecha con `VC_SPR_FLIP_Y` |

No hay rotación en hardware, así que el giro se calcula al cargar la cara:

```c
rot[r][c] = orig[c][7 - r];
```

> Esta fórmula es la que deja los ojos **arriba** (mirando hacia arriba). La
> variante `orig[7-c][r]` es el giro contrario y deja la cara boca abajo — fue un
> error real durante el desarrollo.

---

## 4. Formato de `FACE.DAT`

72 bytes, sin cabecera ni checksum:

| Offset | Bytes | Contenido |
|---|---|---|
| 0-1 | 2 | magic `"FC"` (`'F'`, `'C'`) |
| 2-65 | 64 | las 64 celdas, fila por fila, con los índices de arriba |
| 66 | 1 | **color del cuerpo** (0-3) |
| 67-71 | 5 | reservado (sin usar) |

Se escribe con la ROM API del monitor (`rom_mfs_delete` + `rom_mfs_create_via_zp`
+ `rom_mfs_write_via_zp`), y el mismo buffer que se edita es el que se guarda: no
hay copia intermedia.

**Validación al cargar.** Si el fichero no mide 72 bytes, el magic no cuadra o
alguna celda trae un índice que no sea 0/1/2/4, se **descarta entero** y se usa
la cara de fábrica. Ese último control existe porque las primeras versiones
guardaban los índices corridos (0-3): un fichero así se dibujaría con los colores
cambiados, y es mejor perder la cara que mostrar algo roto.

---

## 5. Color del cuerpo

El recuadro `CUERPO` recorre cuatro colores con el botón de fuego:

**azul → blanco → verde → amarillo**

Y es **un solo color** para todo el gusano. El escollo: las celdas de **fondo** y
los **sprites** viven en espacios de paleta distintos (0-3 y 16-31), así que una
celda no puede usar la paleta de un sprite. La solución es escribir el mismo RGB
en las tres paletas implicadas:

```c
vc_pal_set_bg (PAL_BODY,        1, c);   /* tile del cuerpo en el tablero */
vc_pal_set_spr(PAL_SNAKE_S,    1, c);   /* sprite de la cabeza           */
vc_pal_set_bg (PAL_CANVAS_BODY,1, c);   /* lienzo y recuadro del editor  */
```

Así el recuadro, el lienzo y el gusano en juego enseñan siempre el mismo color.

---

## 6. Al guardar

`G` escribe `FACE.DAT`, vuelve a cargar los patrones **dentro del VBLANK** y
regenera las paletas con `body_apply()`. Si la SD falla se avisa en pantalla
(`ERROR SD`) y **no se sale**, para no perder el dibujo.

---

## 7. Errores que ya se cometieron

Ninguno de estos vuelve a ser un misterio si aparecen:

| Síntoma | Causa |
|---|---|
| Cabeza mirando al revés al subir/bajar | fórmula de giro invertida |
| El cursor «vuela» | bucle del menú leyendo el **nivel** del joystick en vez del **flanco** |
| El cursor se pierde sobre el ojo blanco | marco de un solo color (ahora alterna blanco/negro) |
| Sale basura tras el número de color | `put_str_pal(&c, ...)` con un `char` suelto, no una cadena `\0` |
| El editor se cuelga al arrancar | `vc_pal_set_*` / `vc_load_spr_pattern` llamados **fuera** del VBLANK |
| `FACE.DAT` no aparece en la SD | el emulador abrió **otra** imagen (ver `TIPS-Y-TRAMPA.md` §6.1) |
