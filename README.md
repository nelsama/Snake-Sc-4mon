# SNAKE — Core de Vídeo (vc) para 6502 / Tang Nano 9K

El clásico juego de la serpiente para el computador 6502 con el **Core de Vídeo**
(`videocore-6502-cc65`). Programado en C con cc65 y la biblioteca `vc`.

> **Origen:** este proyecto parte de `videocore-6502-cc65/examples/demo/`, pero se
> ha convertido en un proyecto **autónomo** con su propia lógica, configuración y
> copia de la biblioteca (`lib/vc.lib`, `config/programa.cfg`, `include/video.h`).

---

## Cómo jugar

Se puede jugar con **joystick Atari (DB9)** o con **teclado UART**, en paralelo.

**Joystick Atari** (puerto GPIO del FPGA, ver más abajo):

| Dirección | Acción |
|-----------|--------|
| stick ← / → / ↑ / ↓ | mover izquierda / derecha / arriba / abajo |

**Teclado UART:**

| Tecla | Acción |
|-------|--------|
| `w` / `8` / flecha ↑ | mover arriba |
| `s` / `2` / flecha ↓ | mover abajo |
| `a` / `4` / flecha ← | mover izquierda |
| `d` / `6` / flecha → | mover derecha |
| `q` | salir al monitor |
| `r` | (en GAME OVER) jugar de nuevo |

La serpiente avanza sola. Con el joystick o la tecla eliges la dirección del próximo
paso; no se permite girar 180 grados. Al comer una fruta crece un segmento y sumas
puntos. Pierdes si chocas contra tu propio cuerpo o contra las paredes.

### Reglas

- **Frutas:** hay fresa y manzana (3 colores: rojo/verde/negro); la fruta nueva
  aparece **al azar** cada vez que comes.
- **Niveles:** cada 5 manzanas sube el nivel (hasta 9) y la serpiente va más rápido.
- **Derrota:** choque contra un muro o contra el propio cuerpo (el choque con la
  cola en movimiento no cuenta, porque se mueve ese mismo paso).
- **Victoria:** llenar todo el tablero (raro; el tablero es de 38×22 celdas).
- **Highscore:** se guardan las **5 mejores puntuaciones** de la sesión (en RAM). Al
  morir se muestra la tabla con tu partida **resaltada**. Al salir al monitor
  (`jmp $8000` reinicia la RAM) la tabla se pierde.
- **Fin de partida:** se muestra `GAME OVER` y espera el **botón** (o `R`) para
  reintentar, o `Q` para salir.

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
- Colisiones **en software** contra el array (rápido: len ≤ 836, en la práctica
  unas decenas). Al no comer, la celda de la cola se ignora, porque se mueve.

### El **scroll no se usa**

El scroll (`$D804/$D805`) desplaza la *cámara* sobre el mapa 64×32; es global por
banda, no por objeto, y X envuelve módulo 512 px. El tablero de la serpiente es
fijo y cabe entero en pantalla, así que no aporta nada. Se deja sin bandas.

### Color y paletas

- `BG_COLOR` se reprograma a **negro** (entrada 15 de la paleta de fondo), de
  modo que el tile transparente (índice 0) y los huecos de la fuente se ven sobre
  negro. Se hace dentro del VBLANK.
- La **fuente del sistema** (tile = ASCII) se usa para muros, cuerpo, manzana y
  textos. La fuente pinta siempre el **color 3** de la paleta de la celda, así que
  el color se elige por la **paleta**:
  - Paleta 0 → color 3 = blanco: muros (bloque sólido) y HUD.
  - Frutas → **rojo** (color 1), **verde** (color 2), **negro** (color 3).
  - Serpiente → **azul oscuro** (color 1), con ojo blanco y pupila negra en la
    cabeza.
- La **cabeza** es un sprite; el **cuerpo** y las **frutas** son tiles de fondo.

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
| Sprites (OAM) | 32 | **1** (la cabeza) |
| Patrones de fondo | 256 (fuente en `$20`-`$7F`) | muro, cuerpo, 2 frutas + fuente |
| Patrones de sprite | 64 | 2 (cabeza: derecha y arriba) |
| Celdas de serpiente | 38×22 = 836 | array en RAM (~1.7 KB) |
| RAM | $0800-$3DFF (13.5 KB) | holgado |

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

---

## Compilar

```bash
make CC65_HOME=D:/cc65
```

Genera `output/game.bin`. Cárgalo en el monitor:

```
LOAD GAME 0800
R 0800
```

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
├── game.c              lógica del juego
├── sound.c             driver SID
├── joy.c               driver del joystick Atari (GPIO)
├── startup.s           arranque cc65 -> jmp $8000 (monitor)
├── include/
│   ├── video.h         API del Core de Vídeo (copia de la lib)
│   ├── sound.h         API del driver de sonido
│   ├── joy.h           API del driver del joystick
│   └── romapi.h        ROM API del monitor
├── config/
│   └── programa.cfg    config del linker (copia de la lib)
├── doc/
│   └── VIDEO-LIB.md    manual de la biblioteca
├── build/              objetos
└── output/
    ├── game.bin        binario final
    ├── game.map        mapa de memoria
    └── vc.lib          copia de la biblioteca
```

### Actualizar la biblioteca

```bash
cp ../videocore-6502-cc65/output/vc.lib   lib/vc.lib
cp ../videocore-6502-cc65/src/video.h     include/video.h
cp ../videocore-6502-cc65/config/programa.cfg config/programa.cfg
```
