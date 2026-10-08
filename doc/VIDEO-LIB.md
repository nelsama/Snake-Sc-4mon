# Biblioteca del Core de Vídeo (`vc`)

Librería en **C** con núcleo optimizado en **ensamblador** para programar juegos
sobre el Core de Vídeo del computador 6502 (Sipeed Tang Nano 9K / Gowin GW1NR-9).
Implementa todas las capacidades del core: tiles + tilemap, sprites (OAM), scroll,
split de raster, modo texto, colisión sprite↔tile y paletas programables.

> **Referencia de hardware:** el comportamiento del core (registros, semántica,
> paletas, límites) se documenta en el *Manual de Programación del Core de Vídeo*
> (v2.8, hardware `6502_board_v3`), incluido en este repositorio como
> [`07-MANUAL-PROGRAMACION.md`](07-MANUAL-PROGRAMACION.md). Esta librería lo refleja
> y encapsula su API; no se necesita conocer el VHDL del core.

## Archivos

| Archivo | Rol |
|---------|-----|
| `src/video.h` | API pública: registros, constantes y prototipos |
| `src/video.s` | Núcleo en ensamblador: puerto indirecto, VBLANK, volcados masivos |
| `src/collide.s` | Núcleo en ensamblador: colisiones AABB |
| `src/gfx.c` | Alto nivel en C: texto, helpers de fondo y utilidades |
| `output/vc.lib` | Biblioteca estática reutilizable (video.o + collide.o + gfx.o) |

## Compilación

```bash
make          # compila la BIBLIOTECA -> output/vc.lib
make test     # ejecuta los tests (sim65)
make clean
```

Los **ejemplos** tienen su propio makefile (cada uno en su carpeta); enlazan
contra `../../output/vc.lib`. Por ejemplo:

```bash
cd examples/demo && make     # -> examples/demo/output/demo.bin
```

Ajusta `CC65_HOME` si tu cc65 no está en `D:/cc65`:

```bash
make CC65_HOME=C:/ruta/a/cc65
```

## Áreas de la VRAM (modelo mental)

El core **no tiene framebuffer**: la imagen se genera al vuelo a partir de **4 áreas
lógicas** de memoria de vídeo, que se seleccionan con `$D801`. La librería las
gestiona por ti (`vc_*`); solo necesitas conocerlas para **calcular presupuestos**
(cuántos patrones te caben, cuánta VRAM usas):

| Área | Tamaño | Qué guarda | Funciones típicas |
|------|--------|-----------|-------------------|
| **Tilemap** | 2048 celda(s) | qué tile va en cada celda del mundo 64×32 | `vc_put_cell` |
| **Atributos** | 2048 celda(s) | paleta / flips / PRIO / sólido por celda | `vc_set_cell_attr` |
| **Patrones de fondo** | 256 patrones | los dibujos de los tiles (8×8, 2bpp) | `vc_load_bg_pattern` |
| **OAM** | 160 B (32 sprites) | X / Y / TILE / FLAGS / COLL de cada sprite | `vc_sprite_set` |
| **Patrones de sprite** | 64 patrones | los dibujos de los sprites (8×8, 2bpp) | `vc_load_spr_pattern` |

> **Banco de patrones vs OAM:** el **banco de patrones** son los *dibujos*
disponibles; la **OAM** (32 sprites) son los sprites *colocados* en pantalla. Cada
entrada de la OAM tiene un campo `TILE` que dice cuál de los dibujos usa. Varios
sprites pueden compartir el mismo patrón. (Ver el aviso de límite real en
"Sprites (OAM)" más abajo.)
>
> Detalle completo de direcciones y del encoding de `$D801` en el **manual del core,
> §2.4**. Como usuario de la biblioteca normalmente **no tocas `$D800`/`$D801` a
> mano**: la librería encapsula el puerto indirecto y la dirección alta.

## Uso en un programa nuevo

```c
#include <stdint.h>
#include "video.h"
#include "romapi.h"      /* ROM API del monitor (opcional) */

int main(void) {
    vc_wait_ready();          /* 1. esperar inicialización de VRAM */
    vc_wait_vblank();

    vc_clear_vram();          /* 2. limpiar VRAM (setup por hardware) */
    vc_put_str(2, 2, "HOLA MUNDO");

    while (1) {
        vc_wait_vblank();     /* 3. actualizar SOLO en VBLANK */
        /* ... mover sprites, scroll, HUD ... */
        vc_wait_vblank_end();
    }
    return 0;                 /* al retornar, el monitor recupera el control */
}
```

Enlaza con `output/vc.lib` y usa `config/programa.cfg` como configuración del
linker. Para construir el binario a mano:

```bash
cl65 -t none -O --cpu 6502 -I src -I include -o build/prog.o -c src/prog.c
ld65 -C config/programa.cfg -o output/prog.bin build/prog.o output/vc.lib \
     D:/cc65/lib/none.lib
```

---

## API por bloques

### Sincronización

| Función | Descripción |
|---------|-------------|
| `vc_wait_ready()` | Espera `VIDEO_READY=1` (obligatorio al arrancar) |
| `vc_wait_vblank()` | Espera a **entrar** en VBLANK |
| `vc_wait_vblank_end()` | Espera a **salir** de VBLANK |
| `vc_status()` | Lee `$D803` (bits `VC_STATUS_*`) |
| `vc_wait_setup()` | Espera a que termine el setup de VRAM (`$D817` bit0 `BUSY=0`) |

### Limpieza de VRAM (recomendado al arrancar)

Tras `VIDEO_READY`, la VRAM ya viene inicializada por el hardware (**tilemap a
`$20` = espacios, atributos a 0, fuente cargada**). Aun así, si vas a construir
tu propio mundo, limpia para partir de un estado conocido:

```c
vc_clear_vram();   /* setup por hardware: limpia VRAM + recarga la fuente */
```

`vc_clear_vram()` usa el **setup de VRAM por hardware** del core (`$D816`/`$D817`),
la misma máquina que inicializa la VRAM al arrancar. Limpia tilemap y atributos,
borra los patrones de fondo y de sprite, **vuelve a expandir la fuente de texto**
y deshabilita los 32 sprites. Todo en **~120-275 µs** (en vez del bucle celda a
celda, que tardaría ~ms). Es la forma recomendada de **cambiar de escena**.

> ⚠️ **`vc_clear_vram()` borra los patrones** (los de fondo y los de sprite). El
> setup **recarga la fuente** `$20-$7F` automáticamente, así que el texto sigue
> disponible tras limpiar. **Pero si habías dibujado tus propios tiles en
> `$20-$7F`, se pierden** (ese rango siempre vuelve a ser la fuente).
>
> Mientras el setup corre (`BUSY=1`), las **escrituras del CPU a VRAM/OAM se
> ignoran**; `vc_clear_vram()` **espera a que termine** antes de retornar, así que
> puedes dibujar tu mundo justo después.

Funciones individuales (no usan el setup; control fino):

| Función | Descripción |
|---------|-------------|
| `vc_clear_vram()` | Setup HW: limpia tilemap+attrs+patrones, recarga fuente, OAM off |
| `vc_fill_tilemap(tile)` | Rellena las 2048 celdas del tilemap con `tile` |
| `vc_clear_attr()` | Atributos a 0 (paleta 0, sin flags) |
| `vc_clear_bg_patterns()` | Los 256 patrones de fondo a 0 ⚠️ borra la fuente (recuperable con `vc_clear_vram()`) |
| `vc_clear_spr_patterns()` | El banco de patrones de sprite a 0 |
| `vc_clear_oam()` | Deshabilita los 32 sprites |

> Llama `vc_clear_vram()` **después** de `vc_wait_ready()` y carga tus tiles
> **después** de limpiar.

> **Regla de oro:** mueve sprites, escribe OAM/VRAM y scroll **durante el
> VBLANK**, o verás sprites "partidos" a mitad de frame. Dispara el setup de
> VRAM en VBLANK para evitar el rasgado de ~1 frame mientras limpia.

### Escritura a VRAM (puerto indirecto)

```c
vc_write(area_dir_hi, addr, data);   /* genérico */
```

Macro sin llamada (más rápida, úsala en bucles calientes):

```c
VC_WRITE(area, addr, data);
```

Áreas disponibles (`VC_AREA_*`): `TILEMAP`, `ATTR`, `PAT_BG0`, `PAT_BG1`,
`OAM`, `PAT_SPR0`, `PAT_SPR1`. La dirección alta (bits 2:0) se combina
automáticamente con el área.

### Fondo (tiles y tilemap)

| Función | Descripción |
|---------|-------------|
| `vc_put_cell(col,row,tile)` | Escribe el índice de tile en una celda |
| `vc_put_attr(col,row,attr)` | Escribe el atributo de una celda |
| `vc_set_cell_attr(col,row,paleta,flags)` | Atributo = paleta + `VC_ATTR_*` |
| `vc_free_cell(col,row)` | Deja la celda vacía (tile 0, attr 0) |
| `vc_load_bg_pattern(tile,p0,p1)` | Carga un patrón 8×8 (2 planos) |
| `vc_load_tiles(base,count,p0,p1)` | Carga `count` patrones consecutivos |
| `vc_copy_tilemap(src)` | Copia 2048 bytes de RAM al tilemap |
| `vc_copy_attr(src)` | Copia 2048 bytes de RAM a los atributos |
| `vc_blit_screen(src)` | Copia 40×30 celdas (1200 B) al área visible |

Paletas de fondo (presets por defecto): `VC_BGPAL_0` (azul/cian/blanco),
`VC_BGPAL_1` (marrón/gris/blanco), `VC_BGPAL_2` (verdes), `VC_BGPAL_3` (gris/marrón/verde).
Son índices de paleta reprogramables; ver *Colores: cómo se codifican*.

### Colores: cómo se codifican

El core **no usa RGB directo**: el color se resuelve en **dos capas** (modelo indexado,
como NES). Tú eliges un **índice** y una **paleta**; el RGB lo pone el hardware.

**1. El patrón guarda índices 0-3** (2 bits por píxel, repartidos en `plano0`/`plano1`):

```
color del pixel = (bit_plano1 << 1) | bit_plano0     ->  0..3
```

En los arrays de patrón, cada byte es una fila de 8 píxeles. El índice 0 del **fondo**
y de **sprite** es **transparente**.

**2. La paleta traduce índice -> RGB.** Hay **4 paletas de fondo y 4 de sprite**, en
**bancos separados**:

- **Fondo:** paletas 0-3 (constantes `VC_BGPAL_0`..`VC_BGPAL_3`).
- **Sprite:** paletas 0-3 (constantes `VC_SPPAL_0`..`VC_SPPAL_3`).

> ⚠️ **Los bancos son independientes.** Reprogramar una paleta de fondo **no** afecta
a las de sprite (ni al revés), aunque compartan el mismo número de paleta. Fondo y
sprite usan **entradas de paleta distintas** en el hardware: fondo 0-15, sprite 16-31
(manual §4.2 y §4.4).

Los **nombres de las constantes son neutros** (`VC_BGPAL_*`, `VC_SPPAL_*`) porque las
paletas son **reprogramables**: su color no es fijo. Al arrancar valen estos **presets
por defecto** (manual del core §4):

| Paleta de SPRITE | color0 | color1 | color2 | color3 |
|------------------|--------|--------|--------|--------|
| 0 `VC_SPPAL_0` | — | piel `#FF8800` | marrón `#884400` | negro `#000000` |
| 1 `VC_SPPAL_1` | — | azul `#0000FF` | cian `#00FFFF` | blanco `#FFFFFF` |
| 2 `VC_SPPAL_2` | — | magenta `#FF00FF` | rojo `#FF0000` | blanco `#FFFFFF` |
| 3 `VC_SPPAL_3` | — | verde `#00FF00` | naranja `#FF8800` | blanco `#FFFFFF` |

| Paleta de FONDO | color0 | color1 | color2 | color3 (tinta de texto) |
|-----------------|--------|--------|--------|--------|
| 0 `VC_BGPAL_0` | — | azul | cian | blanco |
| 1 `VC_BGPAL_1` | — | marrón | gris | blanco |
| 2 `VC_BGPAL_2` | — | verde | verde oscuro | verde |
| 3 `VC_BGPAL_3` | — | gris | marrón | verde |

> Los colores de la tabla son **presets**: puedes reescribir cualquier entrada
> (ver *Paletas programables* más abajo) y entonces el nombre `VC_BGPAL_n`/`VC_SPPAL_n`
> seguirá identificando la **paleta n**, pero su color será el que hayas puesto.

**Cómo se usa:**

```c
/* FONDO: el patrón usa indices 0-3; la celda elige la paleta */
vc_load_bg_pattern(tile, plano0, plano1);            /* dibujo (indices 0-3) */
vc_set_cell_attr(col, row, VC_BGPAL_1, 0);           /* paleta 1 (preset: marrón/gris/blanco) */

/* SPRITE: el patron usa indices 0-3; FLAGS elige la paleta */
vc_load_spr_pattern(0, plano0, plano1);              /* dibujo (indices 0-3) */
s.flags = VC_SPPAL_0;                                /* paleta 0 (preset: piel/marrón/negro) */
```

> En resumen: **no escribes RGB en el patrón**. Compones el color con
> `indice (0-3)` + `paleta`. Para cambiar los colores de un dibujo sin redibujarlo,
> cambia la paleta (atributo de la celda o `FLAGS` del sprite) o **reprograma** la
> paleta, no el patrón.

### Paletas programables ($D813-$D815)

Las tablas de arriba son solo los **valores por defecto**. El hardware permite
**reescribir cualquier entrada de paleta** (12 bits **RGB444**), útil para fundidos,
parpadeos, paletas por nivel o tu propia paleta.

- **32 entradas**: **0-15 = fondo**, **16-31 = sprite**. Entrada = `paleta*4 + color`.
- Formato **RGB444** (un nibble por canal): `$F80` = `#FF8800`.
- El puntero **auto-incrementa** al escribir, así que cargar 4 colores seguidos es
  encadenar escrituras.

```c
/* Fijar UN color: paleta 1 de fondo, color 2, a naranja RGB444 = $F80 */
vc_pal_set_bg(1, 2, VC_RGB444(0xF, 0x8, 0x0));

/* Fijar un color de sprite: paleta 1, color 2 (amarillo) */
vc_pal_set_spr(1, 2, VC_RGB888(0x00FF00));   /* desde RGB888 */

/* Cargar los 4 colores de una paleta de fondo de golpe (array RGB444[4]) */
static const uint16_t mi_pal[4] = {
    VC_RGB444(0,0,0), VC_RGB444(0,0,0xA), VC_RGB444(0,0xC,0xF), VC_RGB444(0xF,0xF,0xF)
};
vc_pal_load_bg(0, mi_pal);

/* O al nivel mas bajo, moviendo el puntero a mano */
vc_pal_ptr(VC_PAL_SPR(2, 3));           /* entrada de sprite: paleta 2, color 3 */
vc_pal_set(VC_PAL_SPR(2, 3), 0x0F0);    /* escribe el color y auto-avanza */
```

Macros de color: `VC_RGB444(r,g,b)` (componentes 0-15), `VC_RGB888(0xRRGGBB)`,
`VC_PAL_BG(pal,color)`, `VC_PAL_SPR(pal,color)`.

#### Color de fondo global (`BG_COLOR`)

`BG_COLOR` es lo que se ve en el **margen** y en el **fondo vacío** (tile con color 0
sin sprite detrás). No tiene registro propio: es la **entrada 15** del banco de fondo
(`VC_PAL_BGCOLOR`). Usa el helper dedicado:

```c
vc_set_bgcolor(VC_RGB888(0x000000)); /* fondo negro */
vc_set_bgcolor(VC_BG_COLOR_DEFAULT); /* volver al azul cielo por defecto ($48C) */
```

> ⚠️ La entrada 15 se comparte con el **color 3 de la paleta 3** del fondo: cambiar
> `BG_COLOR` también cambia los tiles que usen "paleta 3, color 3".
>
> Los **sprites NO se ven afectados**: usan un banco de paleta aparte (entradas
> 16-31, manual §4.2), así que su color 3 (entradas 19/23/27/31) es independiente
> de `BG_COLOR`.

> ⚠️ **Escribe las paletas en VBLANK** si cambias muchos colores a la vez: el motor
> aplica el color al vuelo, y hacerlo a mitad de frame puede mostrar una franja con
> el color viejo y otra con el nuevo.

### Sprites (OAM)

```c
vc_sprite_t s = { x_lo, y, tile, flags, coll };
vc_sprite_set(spr, &s);                          /* escribir los 5 campos  */
vc_sprite_move(spr, x, y, VC_SPPAL_2);           /* mover con X de 9 bits  */
vc_sprite_disable(spr);                          /* ocultar                */
vc_sprite16_set(first, x, y, tile0, flags);      /* objeto 16x16 (4 sprites)*/
```

Flags: `VC_SPR_FLIP_Y`, `VC_SPR_FLIP_X`, `VC_SPR_PRIO`, `VC_SPR_SCALE2X`,
`VC_SPR_XBIT8`, paletas `VC_SPPAL_*`.

Puntos de colisión: `VC_COLL_CENTER`, `VC_COLL_FEET`, `VC_COLL_HEAD`,
`VC_COLL_LEFT`, `VC_COLL_RIGHT`, o `VC_COLL_POINT(dx,dy)`.

> La coordenada X es de **9 bits (0-511)**. `vc_sprite_move()` calcula el bit 8
> automáticamente a partir de la X de 16 bits.

> `vc_load_spr_pattern(patron, ...)` maneja automáticamente la **dirección alta**
> (`dir_alta`, bits 2:0 de `$D801`) al escribir el patrón: `patron*8+fila` puede
> llegar a 511 y la función reparte la dirección entre `$D800` y `$D801` por ti.
> Puedes usar **patrón 0..63** (el valor que luego pones en el campo `TILE` del OAM).

### Animación

Animar un sprite = **cambiar su campo `TILE`** entre varios patrones. Carga los
frames como patrones consecutivos y alterna el índice:

```c
/* frames en los patrones 8,9,10 */
vc_load_spr_pattern(8, f0_p0, f0_p1);
vc_load_spr_pattern(9, f1_p0, f1_p1);
vc_load_spr_pattern(10, f2_p0, f2_p1);

/* en el bucle, cada N frames: */
frame = (frame + 1) % 3;
vc_oam_put(spr, VC_OAM_TILE, 8 + frame);   /* TILE = patron del frame */
```

**Flip horizontal** (mirar a la izquierda) con `VC_SPR_FLIP_X` en los flags:

```c
uint8_t f = VC_SPPAL_2;
if (vx < 0) f |= VC_SPR_FLIP_X;
vc_sprite_move(spr, x, y, f);
```

> `vc_sprite_move()` preserva los flags que le pases (paleta, flips, escala,
> prioridad) y solo gestiona el bit 8 de X. El `FLIP_X` (bit 6) no se ve afectado.

Ver el ejemplo completo en [`examples/animation/`](../examples/animation/), que
incluye `piskel2c.py` para convertir archivos **Piskel** a los arrays C de
planos.

### Colisión

**Colisión sprite↔tile (hardware):**

```c
if (vc_solid_hit()) { /* algún sprite tocó un tile sólido este frame */ }
```

- `vc_solid_hit()` devuelve el flag **global** del hardware (no dice cuál sprite
  chocó). Si tienes varios objetos, deduce cuál comparando su borde en RAM con la
  pared y rebota solo ese. Para un único objeto colisionador, el flag basta.

**Colisión sprite↔sprite (software, núcleo en asm):**

El hardware no tiene colisión sprite↔sprite. La biblioteca la resuelve con cajas
AABB, con el núcleo de comparación en ensamblador (`src/collide.s`, rápido):

```c
vc_box_t a, b;
vc_box_from_sprite(&a, ax, ay, 8);    /* caja 8x8 en (ax,ay)  */
vc_box_from_sprite(&b, bx, by, 16);   /* caja 16x16 en (bx,by)*/

if (vc_box_overlap(&a, &b)) {
    /* las dos cajas se solapan */
}

/* test puntual: ¿el centro de a cae dentro de b? */
if (vc_box_contains(&a, ax + 4, ay + 4)) { ... }
```

| Función | Descripción |
|---------|-------------|
| `vc_box_overlap(a, b)` | 1 si las cajas `a` y `b` se solapan (AABB) |
| `vc_box_contains(b, px, py)` | 1 si el punto `(px,py)` cae dentro de `b` |
| `vc_box_from_sprite(box, x, y, size)` | construye una caja cuadrada de `size` px (8/16/32) |

> **AABB:** dos cajas se consideran en colisión si sus rectángulos se solapan. El
> borde exacto **no** cuenta como colisión (cajas adyacentes no chocan).
>
> Se trabaja en píxeles lógicos (X 0-319, Y 0-239), sin signo (así se evitan los
> errores de la aritmética con signo).

> **Rendimiento:** comparar N objetos son N(N-1)/2 pares. Para muchos objetos,
> usa el test puntual (`vc_box_contains`) o agrupa por celdas.

Ejemplo completo en [`examples/collision/`](../examples/collision/).

### Scroll y bandas (split de raster)

```c
vc_set_scroll_x(200);        /* scroll banda media */
vc_set_scroll_y(0);
vc_set_raster(24, 216);      /* fin banda sup / fin banda media (0xFF = off) */
vc_set_band2_scroll(0, 0);   /* scroll de la banda superior (HUD fijo) */
vc_set_band3_scroll(0, 0);   /* scroll de la banda inferior */
```

### Texto

El modo texto es el motor de tiles con la fuente ASCII cargada: **tile = ASCII**.

| Función | Descripción |
|---------|-------------|
| `vc_text_init()` | Limpia la pantalla visible con espacios |
| `vc_put_char(col,row,c)` | Escribe un carácter |
| `vc_put_str(col,row,s)` | Escribe una cadena (termina en `\0`) |
| `vc_put_str_pal(col,row,s,paleta)` | Igual, fijando la paleta del texto |

Rango útil: `$20`-`$7F`.

**Color del texto:** la fuente pinta el **color 3** de la paleta de la celda, así
que el color de la tinta lo elige la **paleta** de cada celda, no el tilemap. Usa
`vc_put_str_pal(col,row,s,paleta)` para fijarla:

```c
vc_put_str_pal(2, 2, "TINTA 0", VC_TINTA(VC_BGPAL_0));  /* paleta 0 */
vc_put_str_pal(2, 3, "TINTA 3", VC_TINTA(VC_BGPAL_3));  /* paleta 3 */
```

El header trae constantes con nombre para no memorizar la tabla:

| Constante | Valor | Significado |
|-----------|-------|-------------|
| `VC_TINTA(pal)` | `pal` (0-3) | paleta de la celda para texto (la fuente pinta color 3) |
| `VC_BGPAL_0` | 0 | paleta de fondo 0 (preset: azul / cian / blanco) |
| `VC_BGPAL_1` | 1 | paleta de fondo 1 (preset: marrón / gris / blanco) |
| `VC_BGPAL_2` | 2 | paleta de fondo 2 (preset: verdes) |
| `VC_BGPAL_3` | 3 | paleta de fondo 3 (preset: gris / marrón / verde) |
| `VC_COLOR0`..`VC_COLOR3` | 0..3 | índice de color **dentro** de una paleta (para patrones de tile) |

> `VC_TINTA(pal)` es un alias de `pal`: la fuente pinta **siempre** el color 3, así
> que el color de la letra lo elige la **paleta** de la celda (p. ej.
> `VC_TINTA(VC_BGPAL_0)`). Si reprogramas esa paleta, el texto cambia de color.

> ⚠️ **Distinción clave:** el **color** de un píxel (0-3) lo determina el **patrón
del tile**; la **paleta** (0-3) la determina el **atributo de la celda**. El color
real que se ve es `paleta[color]`. `VC_COLOR*` es el índice dentro de la paleta;
`VC_BGPAL_*` es cuál de las 4 paletas se usa.
>
> El color 0 del fondo es **transparente**, así que el "hueco" de las letras se ve
> a través de él (fondo o `BG_COLOR`). La fuente estándar **solo** usa el color 3;
> para más colores de texto habría que cambiar las paletas del hardware.
>
> Para control total por celda (paleta + flags, p. ej. `VC_ATTR_PRIO`), usa
> `vc_set_cell_attr(col,row,paleta,flags)` tras escribir el carácter con
> `vc_put_char`.

> ⚠️ **Filas seguras:** usa las filas **1..28**. La fila 0 se ve desplazada por el
> pipeline y la **fila 29 puede recortarse** por overscan del monitor. Ver la
> sección de overscan más abajo.

### Entrada por UART y salida al monitor

La ROM API del monitor da acceso no bloqueante a la UART. Patrón típico para
salir de un juego con una tecla:

```c
#include "romapi.h"

static uint8_t check_quit(void) {
    while (rom_uart_rx_ready()) {
        char c = rom_uart_getc();
        if (c == 'q' || c == 'Q') return 1;
    }
    return 0;
}

int main(void) {
    vc_wait_ready();
    vc_clear_vram();

    while (!check_quit()) {
        vc_wait_vblank();
        /* ... juego ... */
        vc_wait_vblank_end();
    }

    vc_wait_vblank();
    vc_clear_oam();            /* apagar sprites */
    return 0;                  /* startup.s vuelve al monitor ($8000) */
}
```

> Al retornar `main()`, `startup.s` hace `jmp $8000`, que es el monitor. Por eso
> conviene dejar el OAM/scroll en un estado limpio antes de salir.

---

## Detalles de implementación

### Qué está en C y qué en ensamblador

Por rendimiento, el núcleo crítico está en asm; el resto en C. Esta tabla
orienta a quien busque el código de cada función:

| Función | Archivo | Motivo |
|---------|---------|--------|
| `vc_wait_ready/vblank/vblank_end`, `vc_status` | `video.s` | Espera en bucle (rápido) |
| `vc_wait_setup` | `video.s` | Polling del setup de VRAM |
| `vc_write`, `vc_put_cell`, `vc_put_attr` | `video.s` | Puerto indirecto (caliente) |
| `vc_load_bg_pattern`, `vc_load_spr_pattern` | `video.s` | Carga de gráficos |
| `vc_oam_put`, `vc_sprite_move`, `vc_sprite_set`, `vc_sprite16_set` | `video.s` | Sprites (cada frame) |
| `vc_fill_tilemap`, `vc_copy_tilemap`, `vc_copy_attr`, `vc_clear_*` | `video.s` | Volcados masivos |
| `vc_set_scroll_*`, `vc_set_raster`, `vc_set_band*` | `video.s` | Escritura de registros |
| `vc_pal_ptr`, `vc_pal_set`, `vc_pal_load` | `video.s` | Registros de paleta (bajo nivel) |
| `vc_box_overlap`, `vc_box_contains` | `collide.s` + `gfx.c` | Núcleo AABB en asm; wrapper C |
| `vc_clear_vram`, `vc_text_init`, `vc_put_char`, `vc_put_str(_pal)` | `gfx.c` | No crítico, más legible en C |
| `vc_set_cell_attr`, `vc_free_cell`, `vc_sprite_disable` | `gfx.c` | Helpers triviales |
| `vc_pal_set_bg/spr`, `vc_pal_load_bg/spr` | `gfx.c` | Helpers de paleta por (pal,color) |
| `vc_solid_hit`, `vc_box_from_sprite`, `vc_load_tiles`, `vc_blit_screen` | `gfx.c` | Orquestación |

> `vc_clear_vram()` dispara el **setup de VRAM por hardware** (`$D816`) y espera a
> que termine; el trabajo pesado lo hace el core, no el CPU.

**Regla:** si se llama una vez por frame por objeto (sprites, colisiones, puerto
indirecto), está en asm. Si es arranque o helpers, está en C.

### Núcleo en ensamblador (`video.s`, `collide.s`)

Las rutinas sensibles al rendimiento viven en ensamblador:

- **`video.s`**: acceso a VRAM/OAM (puerto indirecto), VBLANK, volcados masivos,
  y las funciones de sprite (`vc_sprite_move`, `vc_sprite_set`,
  `vc_sprite16_set`, `vc_oam_put`).
- **`collide.s`**: núcleo AABB de colisiones (`cl_overlap`, `cl_point`).

`gfx.c` conserva lo que no es crítico (texto, helpers, `vc_clear_vram`, etc.).

Notas:

- El puerto indirecto no auto-incrementa: cada escritura reescribe `$D800/$D801`.
  Los volcados masivos lo hacen una vez por página de 256 bytes.
- Las funciones de sprite usan una subrutina interna `oam_write` (byte =
  `spr*5 + campo`) para no repetir código.
- `calc_pat_base`/`calc_pat_dir` calculan `índice*8 + fila` en 16 bits (necesario
  para tiles ≥ 32).
- Zero Page propia del driver + del módulo de colisiones (~33 bytes), declarada
  en el segmento `ZEROPAGE`, situada antes de la ZP del runtime.

### Tests de los núcleos asm

Los núcleos tienen tests unitarios con `sim65` (sin hardware):

```sh
sh tests/run.sh
```

`video.s` se puede ensamblar con `-D TEST_HOOKS` para **redirigir los registros
`$D800-$D812` a RAM** y poder verificar lo escrito. Ver [`tests/README.md`](../tests/README.md).

### Convención de llamada cc65 (importante si tocas `video.s`)

Este es el punto que más errores causa al escribir asm llamado desde C. cc65 (sin
`__fastcall__` explícito) **no** pasa todos los argumentos por el stack:

> **El ÚLTIMO parámetro llega en registros (A para 8 bits, A/X para 16 bits, lo en
> A y hi en X). Los parámetros ANTERIORES van por el stack software, de izquierda
> a derecha (el primero más abajo; se extrae con `popa`/`popax`).**

Y ojo: si la función tiene **un solo parámetro**, viaja en A/X y **no se pushea**.

| Firma C | cc65 coloca | Leer en asm |
|---------|-------------|-------------|
| `f(uint8_t a)` | A | `A` |
| `f(uint16_t a)` | A=lo, X=hi | `A`, `X` |
| `f(ptr p)` | A=lo, X=hi | `A`, `X` |
| `f(uint8_t a, uint8_t b)` | pusha(a); A=b | `popa`=a, `A`=b |
| `f(uint8_t a, uint16_t b)` | pusha(a); A/X=b | `popa`=a, `A/X`=b |
| `f(uint8_t a, uint8_t b, uint8_t c)` | pusha(a); pusha(b); A=c | `popa`=a, `popa`=b, `A`=c |
| `f(uint8_t a, ptr p, ptr q)` | pusha(a); pushax(p); A/X=q | `popa`=a, `popax`=p, `A/X`=q |
| `f(uint16_t x, uint16_t y)` | pushax(x); A/X=y | `popax`=x, `A/X`=y |

#### Historial de correcciones

La convención de arriba se ha ido corrigiendo con la práctica. Las correcciones
anteriores (paso de argumentos, dirección de patrón para tiles ≥ 32, y `vc_oam_put`
escribiendo el sprite en vez del dato) están documentadas en
[`docs/CHANGELOG.md`](CHANGELOG.md).

> **Regla para añadir funciones:** antes de escribir una rutina asm que se llame
> desde C, compila el `.c` a assembly y mira cómo pasa los argumentos:
>
> ```bash
> cc65 -t none -O --cpu 6502 -o /tmp/x.s src/mi_archivo.c
> # examina las secuencias pusha/pushax/ldax antes del jsr _mi_funcion
> ```
>
> No asumas "todo por stack". El último argumento casi nunca lo está.

### Mapa de memoria (config/programa.cfg)

| Rango | Uso |
|-------|-----|
| `$0002-$001F` | Zero Page del Monitor (NO USAR) |
| `$0020-$00xx` | ZP del driver de vídeo + runtime CC65 |
| `$0800-$3DFF` | RAM del programa (código, datos, BSS) |
| `$3E00-$3FFF` | Stack de CC65 |
| `$D800-$D815` | Registros del Core de Vídeo |
| `$BF00-$BFED` | ROM API del monitor |

### Coste de tamaño

| Módulo | Código |
|--------|--------|
| `video.o` | ~1.0 KB (núcleo asm) |
| `collide.o` | ~170 B (colisiones AABB asm) |
| `gfx.o` | ~930 B (alto nivel C) |
| **Total biblioteca** | **~2.1 KB** de código |
| Binario de un ejemplo | ~3.8–5.7 KB (con startup + main + runtime) |

De la biblioteca se **enlaza solo lo que uses** (es un `.lib`; el linker descarta lo
no referenciado). El archivo `vc.lib` en disco es mayor (~25 KB) porque incluye
símbolos de depuración, que no van al binario final.

---

## Referencia rápida de constantes

```c
/* Áreas del puerto indirecto (para VC_WRITE) */
VC_AREA_TILEMAP  VC_AREA_ATTR  VC_AREA_PAT_BG0  VC_AREA_PAT_BG1
VC_AREA_OAM      VC_AREA_PAT_SPR0  VC_AREA_PAT_SPR1

/* Paletas de fondo / sprite (índices neutros; sus colores son presets) */
VC_BGPAL_0  VC_BGPAL_1  VC_BGPAL_2  VC_BGPAL_3
VC_SPPAL_0  VC_SPPAL_1  VC_SPPAL_2  VC_SPPAL_3
/* Paleta de texto: la fuente pinta color 3 -> VC_TINTA(paleta) */
VC_TINTA(VC_BGPAL_0)  VC_TINTA(VC_BGPAL_3)
/* Índice de color dentro de una paleta (para patrones de tile) */
VC_COLOR0  VC_COLOR1  VC_COLOR2  VC_COLOR3

/* Paletas programables ($D813-$D815) */
VC_RGB444(r,g,b)              /* componentes 0-15 -> RGB444 */
VC_RGB888(0xRRGGBB)           /* RGB888 -> RGB444 */
VC_PAL_BG(pal,color)  VC_PAL_SPR(pal,color)   /* entrada de paleta */
VC_PAL_BGCOLOR (15)   VC_BG_COLOR_DEFAULT (0x48C)
vc_pal_ptr(entrada)  vc_pal_set(entrada,rgb444)  vc_pal_load(entrada,arr,count)
vc_pal_set_bg/spr(pal,color,rgb444)  vc_pal_load_bg/spr(pal,arr4)
vc_set_bgcolor(rgb444)          /* color de fondo global (BG_COLOR) */

/* Setup de VRAM por hardware ($D816/$D817) */
vc_wait_setup()
VC_SETUP_BUSY (0x01)

/* Flags de sprite */
VC_SPR_FLIP_Y  VC_SPR_FLIP_X  VC_SPR_PRIO  VC_SPR_SCALE2X  VC_SPR_XBIT8

/* Flags de atributo de celda (VC_ATTR_*) */
VC_ATTR_PRIO  VC_ATTR_FLIP_Y  VC_ATTR_FLIP_X  VC_ATTR_SOLID

/* Puntos de colisión */
VC_COLL_CENTER  VC_COLL_FEET  VC_COLL_HEAD  VC_COLL_LEFT  VC_COLL_RIGHT
VC_COLL_POINT(dx, dy)

/* Bits de vc_status() */
VC_STATUS_VBLANK  VC_STATUS_OVERFLOW  VC_STATUS_SOLID_HIT  VC_STATUS_VIDEO_READY

/* Geometría */
VC_SCREEN_COLS (40)  VC_SCREEN_ROWS (30)  VC_MAP_COLS (64)  VC_MAP_ROWS (32)
VC_NUM_SPRITES (32)  VC_TILE_BLANK (0)  VC_CHAR_SPACE (0x20)
/* Filas seguras para contenido: 1..28 (ver overscan) */

/* Macros útiles */
VC_CELL(col, row)          // celda del tilemap = row*64 + col
VC_BG_PAT_ADDR(tile, fila) // dirección de patrón de fondo: tile*8 + fila
VC_SPR_PAT_ADDR(patron, fila) // dirección de patrón de sprite: patron*8 + fila
VC_OAM_ADDR(spr, field)    // byte del OAM: spr*5 + field
VC_WRITE(area, addr, data)
```

---

## Ejemplo completo

La demo `examples/demo/main.c` es un ejemplo funcional que usa todo: carga de
tiles, mundo con scroll, sprite animado, objeto 16×16, HUD con split de raster,
texto y colisiones. Úsala como plantilla. El resto de ejemplos
(`collision/`, `animation/`, `palette/`) ilustran cada área por separado.

---

## Limitaciones a recordar

- **Overscan:** el monitor puede recortar los bordes. El core genera 30 filas
  (0-29), pero la fila 0 se ve desplazada por el pipeline y la **fila 29 puede
  quedar fuera** por overscan. **Usa las filas 1..28** para contenido crítico
  (HUD, marcadores); deja 0 y 29 como margen.
- Máx. **8 sprites por línea** (si se supera, `OVERFLOW` y se pierde alguno).
- **32 sprites** en OAM, **64 patrones** de sprite, **256** de fondo.
- La VRAM es **solo de escritura**: mantén tu propia copia del texto/mapa en RAM.
- Colisión sprite↔sprite: **software** (`vc_box_overlap`/`vc_box_contains`).
- Colisión sprite↔tile: flag **global** (`vc_solid_hit`); el software decide quién.
- **`vc_clear_vram()` borra los patrones** (y recarga la fuente `$20-$7F`). Si el
  juego dibuja tiles propios en ese rango, los pierde; usa `$00-$1F` y `$80-$FF`
  para gráficos si necesitas texto.
- La fila 0 del tilemap se ve desplazada por el pipeline: resérvala para HUD y
  dibuja desde la fila 1.
