# SNAKE — Core de Vídeo (vc) para 6502 / Tang Nano 9K

El clásico juego de la serpiente para el computador 6502 con el **Core de Vídeo**
(`videocore-6502-cc65`). Programado en C con cc65 y la biblioteca `vc`.

> **Origen:** este proyecto parte de `videocore-6502-cc65/examples/demo/`, pero se
> ha convertido en un proyecto **autónomo** con su propia lógica, configuración y
> copia de la biblioteca (`lib/vc.lib`, `config/programa.cfg`, `include/video.h`).

> 📌 **¿Vas a empezar otro juego?** Lee antes
> [`doc/TIPS-Y-TRAMPA.md`](doc/TIPS-Y-TRAMPA.md): guía **general** de desarrollo en
> el Core de Vídeo (tiles, sprites, flips, paletas, texto, SID, depuración) con
> síntoma → causa → solución. Los ejemplos concretos son ilustrativos, no de este
> juego.

---

## Cómo jugar

Al arrancar aparece un **menú** con tres opciones, que se eligen con el stick o
W/S y se confirman con fuego o ENTER:

| Opción | Qué hace |
|--------|----------|
| **JUGAR** | empieza una partida |
| **CREAR CARA** | abre el editor de la cara del gusano |
| **SALIR** | despedida y vuelta al monitor con autoboot |

Se puede jugar con **joystick Atari (DB9)** o con **teclado UART**, en paralelo.

**Joystick Atari** (puerto GPIO del FPGA, ver más abajo):

| Control | Acción |
|---------|--------|
| stick ← / → / ↑ / ↓ | mover izquierda / derecha / arriba / abajo |
| fuego | confirmar en el menú |

**Teclado UART:**

| Tecla | Acción |
|-------|--------|
| `w` / `8` / flecha ↑ | mover arriba |
| `s` / `2` / flecha ↓ | mover abajo |
| `a` / `4` / flecha ← | mover izquierda |
| `d` / `6` / flecha → | mover derecha |
| `q` | salir al monitor |
| `r` | (en GAME OVER) volver al menú |

La serpiente avanza sola. Con el joystick o la tecla eliges la dirección del próximo
paso; no se permite girar 180 grados. Al comer una fruta crece un segmento y sumas
puntos. Pierdes si chocas contra tu propio cuerpo o contra las paredes.

### Reglas

- **Frutas:** hay fresa y manzana (3 colores: rojo/verde/negro); la fruta nueva
  aparece **al azar** cada vez que comes.
- **Niveles:** cada 5 manzanas sube el nivel (hasta 9) y la serpiente va más rápido.
- **Derrota:** choque contra un muro o contra el propio cuerpo (el choque con la
  cola en movimiento no cuenta, porque se mueve ese mismo paso).
- **Victoria:** llegar a **120 segmentos** (`MAX_SNAKE`), que es el tope del array.
  El tablero tiene 38×22 = 836 celdas, así que el tope lo pone la RAM, no el tablero.
- **Highscore:** se guardan las **5 mejores puntuaciones** de la sesión (en RAM) con
  el **largo alcanzado** en cada una. Al morir se muestra la tabla con tu partida
  **resaltada**. Al reiniciar la RAM la tabla se pierde.
- **Fin de partida:** se muestra `GAME OVER` con la tabla y se espera el **botón**
  (o `R`) para **volver al menú**, o `Q` para salir al monitor.
- **Al salir:** se muestra una despedida y el monitor **reinicia con autoboot**
  (`BOOT.INI`), volviendo al launcher/menú (ver abajo).

---

## Diseño del juego (decisiones fijadas)

### El tablero son **tiles** del tilemap; la serpiente = **cadenas de celdas**

El hardware dibuja con un motor de tiles + sprites (sin framebuffer). El cuerpo
de la serpiente puede ser muy largo, así que **no cabe como sprites** (solo hay
32 slots de OAM y 8 por línea). Por eso:

- **Cuerpo y frutas → tiles** del tilemap (`vc_put_cell`).
- **Cabeza → sprite** (OAM slot 0): tiene **4 orientaciones** (derecha / izquierda /
  arriba / abajo). Se dibuja un patrón explícito para derecha y arriba, y las
  contrarias con flip (`VC_SPR_FLIP_X` / `VC_SPR_FLIP_Y`).

Cada paso se tocan **2-3 celdas** (nueva cabeza, el segmento siguiente —que pasa
a cuerpo— y la cola que se recicla). Es un coste mínimo para el 6502.

### Codificación de la serpiente

- `snake[0]` = cabeza, `snake[len-1]` = cola (array de celdas `(x,y)`).
- Al moverse, los segmentos se desplazan una posición y la cabeza ocupa la celda
  nueva. Si come, **no** se borra la cola y el array crece.
- Colisiones **en software** contra el array (rápido: len ≤ 120, en la práctica
  unas decenas). Al no comer, la celda de la cola se ignora, porque se mueve.

### El **scroll no se usa**

El scroll (`$D804/$D805`) desplaza la *cámara* sobre el mapa 64×32; es global por
banda, no por objeto, y X envuelve módulo 512 px. El tablero de la serpiente es
fijo y cabe entero en pantalla, así que no aporta nada. Se deja sin bandas.

### Color y paletas

- `BG_COLOR` se reprograma a **negro** (entrada 15 de la paleta de fondo), de
  modo que el tile transparente (índice 0) y los huecos de la fuente se ven sobre
  negro. Se hace dentro del VBLANK.
- La **fuente del sistema** (tile = ASCII) se usa para muros y textos. La fuente
  pinta siempre el **color 3** de la paleta de la celda, así que el color se elige
  por la **paleta**:
  - Paleta 0 → color 3 = blanco: muros (bloque sólido) y HUD.
  - Frutas → **rojo** (color 1), **verde** (color 2), **negro** (color 3).
- La **cabeza** es un sprite; el **cuerpo** y las **frutas** son tiles de fondo.

> ⚠️ En las paletas de **fondo** el **color 0 es siempre transparente** (se ve
> `BG_COLOR`) y no se puede pintar. Los colores útiles son 1, 2 y 3.

### La cara del gusano es editable

La cabeza no es un dibujo fijo: el jugador la diseña dentro del juego y se guarda
en la SD. El detalle está en **`doc/EDITOR-DE-CARA.md`**; en resumen:

- Un **lienzo de 8×8** (64×64 px) con 4 «colores»: **0 transparente, 1 cuerpo,
  2 pupila, 4 ojo** (el 3 queda libre).
- Con **un solo glifo mirando a la derecha** se sacan las cuatro direcciones: las
  otras tres salen de girarlo 90° en software (la de arriba) y de los flips del
  sprite (`VC_SPR_FLIP_X` para izquierda, `VC_SPR_FLIP_Y` para abajo).
- Se guarda en **`FACE.DAT`** (72 B) en la SD mediante la ROM API del monitor.
- La **cara de fábrica** se usa si el fichero no existe o no es válido.

### Color del cuerpo, elegible

Junto al lienzo hay un recuadro **`CUERPO`**; el cursor se mueve libre entre el
8×8 y ese recuadro, y con el botón de fuego se cambia el color del cuerpo:
**azul → blanco → verde → amarillo**.

Es **un solo color** para todo el gusano. Como las celdas de fondo y los sprites
usan espacios de paleta distintos, no pueden compartir la paleta: `body_apply()`
escribe el mismo RGB en las tres paletas que lo usan (tile del cuerpo, sprite de
la cabeza y lienzo del editor). El índice se guarda en `FACE.DAT`.

### Sonido (SID 6581, compatible C64)

El juego usa el **SID** mapeado en `$D400-$D41F` (idéntico al C64). Driver en
`sound.c` / `include/sound.h`. Voces:

| Voz | Uso |
|-----|-----|
| 2 | comer manzana (blip) y choque/muerte (ruido) |
| 3 | subir de nivel (bono) |

El volumen master se fija a máximo (`$D418 = 0x0F`, filtro desactivado).
`snd_update()` se llama una vez por frame para gestionar las envolventes.

> La **voz 3** (oscilador de ruido) también se usa como fuente de aleatoriedad
> (`snd_random()`, registro `$D41B`, "OSC3 random") para sembrar el RNG.

### Entrada: joystick Atari (DB9) + teclado UART

Driver en `joy.c` / `include/joy.h`. El joystick y el teclado funcionan **en
paralelo**; el juego solo lee un bitmask de acciones (`JOY_A_*`) en `read_input()`.

| Recurso | Valor |
|---|---|
| Puerto GPIO | Puerto 1 |
| Datos | `$C000` (lectura = entradas) |
| Config por bit | `$C002` (0 = salida, 1 = entrada) |
| Bits del joystick | 3-7 (los 0-2 los ocupa el TM1638) |

Señal → bit: right = bit 3 (`0x08`), left = 4 (`0x10`), down = 5 (`0x20`),
up = 6 (`0x40`), fire = 7 (`0x80`). El joystick Atari es **activo por nivel bajo**
(`JOY_ACTIVE_LOW`).

> ⚠️ `$C002` se escribe siempre con **read-modify-write** (preserva los bits 0-2 del
> TM1638); si se pisa con un valor fijo, el TM1638 deja de funcionar. Igual al leer:
> se enmascara con `JOY_MASK` (`0xF8`).

Para adaptarlo a otra placa solo hay que ajustar las direcciones `$C000`/`$C002` y
las máscaras en `include/joy.h`.

### Presupuesto de recursos

| Recurso | Límite | Uso previsto |
|---|---|---|
| Sprites (OAM) | 32 | **2** (la cabeza y el cursor del editor) |
| Patrones de fondo | 256 (fuente en `$20`-`$7F`) | muro, cuerpo, 2 frutas, 3 sólidos del lienzo + fuente |
| Patrones de sprite | 64 | 3 (cabeza derecha, cabeza arriba, cursor) |
| Celdas de serpiente | `MAX_SNAKE` = **120** | array en RAM (240 B) |
| RAM | `$0800-$3DFF` (13.5 KB) | **casi lleno**: quedan ~15 B |

> ⚠️ La RAM está al límite. `MAX_SNAKE` se bajó de 400 a 120 justo por eso, y el
> linker avisa con `Segment 'BSS' overflows memory area 'RAM'` en cuanto se añade
> una variable. Si hace falta más sitio, la vía es **empaquetar cada celda en un
> byte** (las coordenadas caben en 6 bits cada una), lo que liberaría la mitad de
> ese array sin recortar el largo.

---

## Notas sobre el Core de Vídeo

### Píxel fantasma en el borde derecho de un sprite — RESUELTO en el core

**Síntoma (histórico):** un sprite de 8×8 con contenido en su última columna
(columna 7) mostraba una **línea de 1 píxel** a su derecha, independiente del flip
X/Y. Solo afectaba a sprites (los tiles de fondo se veían bien).

**Causa:** desalineación de 1 píxel del pipeline de sprites respecto al fondo: una
etapa B redundante (`spr_active1`/`spr_pixcode1`) hacía que el sprite saliera en
N+3 en vez de N+2. Se corrigió en `src/hdmi/video_core.vhd` eliminando esa etapa
redundante.

**Estado actual:** resuelto en hardware. Los patrones de sprite usan su columna 7
con normalidad (el workaround de software que se aplicó temporalmente ya se
retiró).

> Detalle completo (diagnóstico, reproducción y el fix aplicado):
> `doc/CORE-BUG-sprite-right-edge.md`.

### Salida: reinicio con autoboot

Al terminar (o salir con `q`) el juego vuelve al monitor **pidiendo autoboot**, de
modo que el monitor relee `BOOT.INI` y lanza el programa de arranque (el launcher):

```c
#define SYSFLAGS (*((volatile uint8_t *)0xC004))

SYSFLAGS = 0x00;          /* bit 0 = 0 -> permitir autoboot */
__asm__("jmp ($FFFC)");   /* reset por el vector -> monitor -> BOOT.INI */
```

El flag `$C004` bit 0 es un registro de hardware **fuera del dominio de reset**, así
que sobrevive al reinicio (la RAM no serviría: el stack de cc65 la pisa).

> ⚠️ Hay que usar **`jmp ($FFFC)`** (vector RESET) y no `jmp $8000`: solo el primero
> hace que el monitor evalúe el flag recién escrito.
>
> - `SYSFLAGS = 0x01` → monitor con prompt (sin autoboot).
> - `SYSFLAGS = 0x00` → monitor **con** autoboot (`BOOT.INI`).

---

## Compilar

```bash
make CC65_HOME=D:/cc65
```

Genera `output/game.bin`. Cárgalo en el monitor:

```
LOAD SNAKE.BIN
R 0800
```

O vía el launcher de la SD (arranca solo con `BOOT.INI`).

**Probándolo en el emulador** (`EmuladorFpga6502`), sin pasar por el launcher:

```bash
python tools/emu_agent.py start --sd D:/Proyectos/EmuladorFpga6502/tarjeta.img
curl --data-binary @output/game.bin "http://localhost:8080/run?name=SNAKE.BIN&addr=0800"
```

> ⚠️ La imagen SD va con **ruta absoluta**. Con ruta relativa el emulador abre
> otro directorio y crea una tarjeta **vacía** (el juego ve 0 ficheros). Detalle
> en `doc/TIPS-Y-TRAMPA.md` §6.1.
>
> Si `BOOT.INI` apunta al launcher, el autoboot se lo come y `/run` no arranca el
> juego: para iterar, quita `BOOT.INI` con
> `DELETE /sdcard/delete?name=BOOT.INI` y reponlo al terminar.

> **Nota sobre `CC65_HOME`:** en este entorno cc65 es un ejecutable de Windows
> (`D:/cc65`). Si compilas desde **Git Bash / cmd**, usa `CC65_HOME=D:/cc65`.
> Desde **WSL** no se pueden lanzar los `.exe`, así que compila desde el shell
> nativo de Windows.

> **Versión de la API:** el juego está alineado con la biblioteca `vc`
> correspondiente al **Manual del Core v2.7**. En esa versión las constantes de
> paleta usan **nombres neutros** (`VC_BGPAL_0..3`, `VC_SPPAL_0..3`) y
> `VC_TINTA(paleta)`.

---

## Estructura

```
snake/
├── makefile            build autónomo (ROOT = .)
├── game.c              lógica del juego y editor de cara
├── sound.c             driver SID
├── joy.c               driver del joystick Atari (GPIO)
├── startup.s           arranque cc65 -> jmp $8000 (monitor)
├── lib/
│   └── vc.lib          copia de la biblioteca
├── include/
│   ├── video.h         API del Core de Vídeo (copia de la lib)
│   ├── sound.h         API del driver de sonido
│   ├── joy.h           API del driver del joystick
│   └── romapi.h        ROM API del monitor
├── config/
│   └── programa.cfg    config del linker (copia de la lib)
├── doc/
│   ├── VIDEO-LIB.md    manual de la biblioteca
│   ├── EDITOR-DE-CARA.md  editor, formato de FACE.DAT y color del cuerpo
│   ├── TIPS-Y-TRAMPA.md   lecciones y trampas (para empezar un juego nuevo)
│   └── CORE-BUG-sprite-right-edge.md
├── build/              objetos
└── output/
    ├── game.bin        binario final
    └── game.map        mapa de memoria
```

### Actualizar la biblioteca

```bash
cp ../videocore-6502-cc65/output/vc.lib   lib/vc.lib
cp ../videocore-6502-cc65/src/video.h     include/video.h
cp ../videocore-6502-cc65/config/programa.cfg config/programa.cfg
```
