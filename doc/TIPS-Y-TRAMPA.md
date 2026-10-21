# Tips y trampas — desarrollo de juegos en el Core de Vídeo

Guía práctica para **arrancar un juego nuevo** en el Core de Vídeo (`vc`), cc65,
6502 (Tang Nano 9K). Recoge problemas que aparecen una y otra vez, con **síntoma
→ causa → solución**. Los ejemplos concretos (colores, nombres de sprites, etc.)
son solo ilustraciones: adáptalos a tu juego.

---

## 1. Puesta en marcha del proyecto

### 1.1 Compila desde el shell nativo de Windows
cc65 aquí son **`.exe` de Windows** (`D:/cc65/bin/cl65.exe`). Desde **WSL / Git
Bash** no se pueden lanzar (`Error 127: No such file or directory`).

```bash
make CC65_HOME=D:/cc65        # BIEN
```

### 1.2 Decidir cómo referenciar la librería
Dos estrategias, elige una conscientemente:

- **Proyecto autónomo** (recomendado): copia `vc.lib`, `video.h` y
  `programa.cfg` dentro de tu proyecto y enlaza contra las copias locales.
  No dependes de rutas relativas. **Costo:** al actualizar la lib, recopiar a mano.
- **Referencia directa** al repo de la librería. Cómodo para actualizar, más
  frágil si mueves carpetas.

Verifica que tu copia local es la correcta:

```bash
cmp lib/vc.lib ../videocore-6502-cc65/output/vc.lib && echo identico
```

### 1.3 `main()` retorna → salta al monitor
El `startup.s` típico hace `jsr _main` y, al volver, `jmp $8000`. **Antes de
salir, deja el estado limpio:** apaga sprites, scroll a 0, silencia el sonido.
Si tu programa corre bajo un launcher/menú, ajusta esa dirección de salto a la
que use tu sistema.

### 1.4 La RAM es de sesión
Los datos en RAM se pierden al salir al monitor (se reinicia la RAM). Para
persistencia real, guarda en SD (`mfs_create` / `mfs_write`).

### 1.5 Salir al monitor: prompt o autoboot
El monitor tiene un **flag de usuario en hardware (`$C004` bit 0)** que decide si,
al reiniciar, ejecuta el **autoboot** (lee `BOOT.INI` y lanza ese programa). Vive
**fuera del dominio de reset**, así que sobrevive al reinicio (la RAM no vale: el
stack de cc65 la pisa).

| `$C004` bit 0 | Al reiniciar |
|---|---|
| `1` | monitor con **prompt** (sin autoboot) |
| `0` | monitor **con autoboot** (lanza `BOOT.INI`) |

```c
#define SYSFLAGS (*((volatile uint8_t *)0xC004))

/* volver al launcher / BOOT.INI */
SYSFLAGS = 0x00;
__asm__("jmp ($FFFC)");   /* reset por el vector RESET */

/* volver al prompt del monitor */
SYSFLAGS = 0x01;
__asm__("jmp ($FFFC)");
```

**Trampa clave:** usa **`jmp ($FFFC)`**, no `jmp $8000`. Solo el primer camino pasa
por el vector RESET, y solo así el monitor evalúa el flag recién escrito.

> **Ejemplo de uso:** un juego en `BOOT.INI` que, al terminar, quiere volver al
> menú (que es otro `.BIN`): al salir deja `SYSFLAGS = 0` y reinicia por el vector;
> el monitor relee `BOOT.INI` y relanza el menú.

---

## 2. Gráficos: tiles, patrones, paletas

### 2.1 Rangos de tiles: no confundas la fuente con tus dibujos
La **fuente de texto** ocupa el rango **`$20`-`$7F`** (el índice del tile es el
ASCII del carácter). Tus **tiles propios** (dibujos a mano) van **fuera** de ese
rango — usa el espacio libre bajo la fuente, p. ej. `$01`-`$1F`.

**Trampa típica:** escribir un "carácter" con el tile equivocado. `'A'` es
`0x41`, no `0x01`; `'O'` es `0x4F`, no `0x0F`. Usar los bajos equivale a
patrones vacíos → **no se ve nada** y parece un bug del motor.

> **Ejemplo:** un juego dibujaba el muro con el tile `0x0F` "pensando en la
> letra A"; el resultado era un tablero invisible. Bastó usar `0x41` (fuente) o
> definir un tile propio en `$0F`.

### 2.2 Codificación del patrón: 2 planos, índices 0-3
Cada patrón son **dos arrays `uint8_t[8]`** (plano 0 y plano 1). Cada byte es una
**fila de 8 píxeles**, con **bit 7 = píxel izquierdo**. El índice de color es:

```
índice = (bit_plano1 << 1) | bit_plano0      0..3      (0 = transparente)
```

- Dibujo de **un color** (índice 3): `plano0 = plano1 = dibujo`.
- Dibujo en el **índice 1**: `plano0 = dibujo`, `plano1 = 0x00`.

### 2.3 El color lo da la PALETA, no el patrón (¡esto es lo normal!)
El patrón solo guarda índices 0-3. El RGB lo pone la **paleta**, que es
**reprogramable**. Por eso **no** cambies el patrón para recolorear: cambia la
paleta.

```c
vc_pal_set_bg(paleta, indice, VC_RGB444(r,g,b));   /* tiles de fondo */
vc_pal_set_spr(paleta, indice, VC_RGB444(r,g,b));  /* sprites        */
```

Hay 4 paletas de fondo y 4 de sprite. Dos celdas con el **mismo tile** pueden
verse de colores distintos si usan paletas distintas — patrón muy útil.

> **Ejemplo:** dos "frutas" distintas eran el mismo dibujo de 3 colores; se
> diferenciaban por paleta. Y varios objetos compartían la letra `'O'` como forma,
> con colores diferentes por paleta.

### 2.4 El texto de la fuente usa SIEMPRE el color 3
La fuente del sistema pinta **solo el color 3** de la paleta de la celda. No se
puede colorear un carácter por su patrón. Para colorear texto, cambia la
**paleta de esa celda**:

```c
vc_set_cell_attr(col, row, paleta, 0);
/* o el helper: */
vc_put_str_pal(col, row, "TEXTO", VC_TINTA(paleta));
```

**Corolario:** un mismo tile (una letra) en paletas distintas = colores distintos.

### 2.5 `BG_COLOR` es global y COMPARTE entrada con la paleta 3
`BG_COLOR` es la **entrada 15** = "paleta 3, color 3". Es el color que se ve
donde el tile es transparente (índice 0) y en el margen de pantalla. Si usas la
**paleta 3** para contenido, cambiar el fondo **también** cambia su color 3.

- Para fondo uniforme: `vc_set_bgcolor(rgb444)` dentro del VBLANK.
- **Truco:** reserva las paletas **0/1/2** para tu contenido y deja la 3 como
  "fondo/margen".
- Los **sprites no se ven afectados** (usan banco de paleta aparte).

### 2.6 `vc_clear_vram()` borra los PATRONES (recarga la fuente)
`vc_clear_vram()` usa el setup por hardware: limpia tilemap, atributos, OAM y
**borra los patrones**, y **vuelve a expandir la fuente** `$20-$7F` (~120-275 µs).

- Carga tus tiles propios **después** de limpiar.
- Si dibujaste en `$20-$7F`, se pierden.
- Mientras corre (`BUSY`), las escrituras del CPU a VRAM/OAM **se ignoran**; la
  función espera a que termine, así que puedes dibujar justo después.

### 2.7 Orden de arranque del vídeo
```c
vc_wait_ready();        /* esperar VIDEO_READY */
vc_wait_vblank();
vc_clear_vram();        /* limpiar (setup HW) */
vc_set_bgcolor(...);    /* fondo */
/* cargar paletas, cargar patrones, dibujar el mundo/UI */
vc_wait_vblank_end();
```

---

## 3. Sprites (OAM)

### 3.1 Presupuesto: 32 sprites, máximo 8 por línea
Si pones más de 8 sprites en la **misma línea horizontal**, el core marca
`OVERFLOW` y los sobrantes no se dibujan. Para muchas cosas alineadas (un
ejército, un fondo de objetos) usa **tiles**.

> **Regla práctica:** objetos "pocos y móviles libres" → **sprites**; muchos
> objetos en rejilla, o el cuerpo largo de un personaje → **tiles**.

### 3.2 El espejo/giro con FLIP solo es fiable en SPRITES
Los **atributos de tile de fondo** documentan bits de flip X/Y, pero en la
práctica **no voltean de forma fiable**. Los **sprites** sí: usa
`VC_SPR_FLIP_X` / `VC_SPR_FLIP_Y` en sus `FLAGS`.

> **Regla:** **todo lo que tenga que girar/espejarse, que sea sprite.** Si un
> objeto grande giratorio no cabe como sprites, dibújalo con **4 patrones**
> (uno por orientación) en tiles, en vez de esperar que el flip del tile funcione.

### 3.3 No hay rotación de 90° — solo espejos X/Y
Con 4 direcciones necesitas al menos **un patrón por eje** + flips para el
contrario:

- Base "mira a la derecha" → izquierda = `FLIP_X`.
- Base "mira arriba"   → abajo     = `FLIP_Y`.

**Trampa sutil:** si tu sprite es **simétrico en un eje**, el flip de ese eje se
ve **idéntico** y parece que "no funciona". Para el eje vertical necesitas un
**dibujo distinto** (mirando arriba), no solo el flip.

> **Ejemplo:** una cabeza con los ojos a un lado se veía bien al voltear en
> horizontal, pero el volteo vertical no cambiaba nada porque el dibujo era
> simétrico arriba-abajo. Se resolvió dibujando también la versión "mirando
> arriba" y usando el flip solo para el sentido contrario.

### 3.4 Escribe la OAM **dentro del VBLANK** (o los sprites fallan)
Si mueves un sprite **después** de escribir muchas celdas de VRAM, puede que el
VBLANK ya haya acabado → el sprite no se aplica o sale partido.

- Toca OAM/VRAM/scroll **dentro** de `vc_wait_vblank()` … `vc_wait_vblank_end()`.
- En pantallas **estáticas** con sprite, **recolócalo cada frame** para que no se
  pierda.

> **Ejemplo:** una pantalla de despedida dibujaba el sprite una sola vez tras
> rellenar todo el fondo; no se veía. Se arregló recolocándolo dentro del bucle
> de frames.

### 3.5 Verifica que la librería escribe el valor correcto ANTES de culpar al hardware
Hubo un **bug en `vc_oam_put`**: escribía el **índice del sprite** en lugar del
**dato**, así que el sprite 0 **siempre dibujaba el patrón 0** (parecía que el
hardware ignoraba el tile). Se corrigió en la librería.

> **Lección general:** ante un "el hardware ignora X", **primero** comprueba que
> tu código/librería escribe X bien. Un byte mal pasado puede parecer un bug de HW.
> Mantén la librería actualizada y prueba casos simples de forma aislada.

---

## 4. Entrada y HUD / texto

### 4.1 Tu función de texto: ¿envuelve o recorta?
Si al llegar a la columna 40 salta a la fila siguiente, **cualquier texto que no
quepa se descuadra** (aparece partido en otra fila). Casi siempre quieres que
**recorte**:

```c
while (*s && col < VC_SCREEN_COLS) { /* escribir */ col++; s++; }   /* recorta */
```

### 4.2 Centrar bien
Centra por cálculo, no a ojo: `col = (40 - len) / 2`. Si además hay un parpadeo
que borra el texto, **borra la misma zona** que dibujaste (misma col y longitud).

### 4.3 Filas seguras: 1..28
La **fila 0** se ve desplazada por el pipeline y la **fila 29** puede recortarse
por overscan. Mantén texto y UI en las filas **1..28**.

### 4.4 Periféricos mapeados en memoria: usa read-modify-write
Si compartes un registro con otros periféricos (p. ej. bits de un joystick +
bits de otro chip), **no pises** los ajenos: haz RMW y **enmascara al leer**.
Un valor fijo escrito de golpe puede romper otro periférico.

> **Ejemplo:** el registro de configuración del puerto GPIO se escribía con RMW
> para no tocar los bits de un display TM1638 que compartía el mismo registro.

---

## 5. Sonido (SID)

### 5.1 Registros (SID 6581, mapeado como C64: `$D400-$D41F`)
- `$D400 + 7*voz + 0/1` = frecuencia (lo/hi).
- `+4` = CTRL: `bit0 GATE`, `bits 7:4 forma` (`TRI 0x10`, `SAW 0x20`,
  `PULSE 0x40`, `NOISE 0x80`).
- `+5`/`+6` = attack/decay y sustain/release.
- `$D418` = volumen master + filtro.

**Patrón de efectos "percutivos":** dispara la voz con `GATE=1`, y en un
`snd_update()` por frame decrementa un timer; al llegar a 0, baja el `GATE`
(deja que el release apague la voz). No bloquees el juego esperando al sonido.

### 5.2 Aleatoriedad de verdad: el oscilador de la voz 3 (`$D41B`)
El core expone el **oscilador 3** como fuente de azar. Pon la voz 3 en modo
RUIDO **sin gate** (para que no suene) y lee el registro:

```c
/* Voz 3 en modo ruido, sin GATE (silenciosa) */
escribir(SID_V3 + 1, 0xFF); escribir(SID_V3 + 0, 0xFF);   /* frecuencia alta */
escribir(SID_V3 + 4, 0x80);                               /* WF_NOISE */
uint8_t r = leer(0xD41B);                                 /* bits aleatorios */
escribir(SID_V3 + 4, 0x00);                               /* apagar */
```

**Trampa clásica:** sembrar el RNG con una **constante fija** hace que todo salga
**siempre en el mismo sitio** (el "azar" repite la misma secuencia). **Siembra
no determinista**: mezcla `$D41B` con `rom_get_micros()` (temporizador real) y un
contador de arranques.

> **Ejemplo:** los objetos aparecían siempre en las mismas posiciones porque la
> semilla era fija; se resolvió sembrando con `$D41B` + `rom_get_micros()`.

---

## 6. Depuración: orden de sospechas

1. **¿Compila/enlaza?** Revisa `output/game.map`: tamaños de segmentos y que no
   desborde la RAM (`$0800-$3DFF`).
2. **¿No se ve un tile?** índice fuera de rango; dibujo en `$20-$7F` (fuente);
   índice 0 (transparente); atributo con paleta cuyo color es el fondo; celda sin
   escribir.
3. **¿No se ve un sprite?** OAM escrita **fuera** del VBLANK; `Y` ≥ 248
   (deshabilitado); >8 en la misma línea (`OVERFLOW`); dato mal escrito a la OAM.
4. **¿Nada reacciona a un cambio?** Antes de culpar al hardware, comprueba que la
   **librería escribe** ese valor (lección §3.5).
5. **¿Texto descolocado?** envoltura (§4.1) o filas 0/29 (§4.3).
6. **¿Queda un elemento tras cambiar de pantalla?** casi siempre un **sprite que
   no apagaste** (`vc_sprite_disable`) o VRAM sin limpiar.
7. **¿Aleatorio repetido?** semilla fija (§5.2).

---

## 7. Checklist para un juego nuevo

- [ ] Proyecto compilable desde shell Windows (`CC65_HOME=D:/cc65`).
- [ ] Decidido: lib local copiada o referenciada (y anotado cómo actualizarla).
- [ ] `setup_video()`: `wait_ready` → `vblank` → `clear_vram` → fondo → paletas → patrones.
- [ ] Fondo con `vc_set_bgcolor`; contenido en paletas 0/1/2 (dejar la 3 al margen).
- [ ] Tiles propios fuera de `$20-$7F`; letras dentro de `$20-$7F`.
- [ ] Todo el movimiento de OAM/VRAM/scroll dentro del VBLANK.
- [ ] Lo que gira/espeja → sprite con `VC_SPR_FLIP_*` (o 4 patrones si es tile).
- [ ] Texto: función que **recorta**, centrado por `(40-len)/2`, filas 1..28.
- [ ] RNG sembrado con `$D41B` + `rom_get_micros()` (nunca semilla fija).
- [ ] Salida limpia: `vc_clear_oam()` + scroll 0 + silenciar sonido.
- [ ] Decidido si al salir se vuelve al **prompt** (`$C004=1`) o al **autoboot**
      (`$C004=0`), reiniciando con `jmp ($FFFC)` (no `jmp $8000`).
- [ ] Recursos dentro de límites: ≤32 sprites (≤8/línea), ≤256 patrones de fondo, ≤64 de sprite.
- [ ] Persistencia: la RAM es de sesión; usar SD si se necesita.

---

## 8. Referencia rápida

### Registros
| Registro | Uso |
|---|---|
| `$D800/$D801/$D802` | puerto indirecto VRAM/OAM/patrones |
| `$D803` | STATUS: VBLANK, OVERFLOW, SOLID_HIT, VIDEO_READY |
| `$D804-807` | scroll X/Y |
| `$D809/$D80E` | raster (split en bandas) |
| `$D813-815` | paletas programables (`$D813` = índice de entrada) |
| `$D816/$D817` | setup de VRAM por hardware (disparo / BUSY) |
| `$C004` | flag de usuario: bit 0 = autoboot al reiniciar (0 = sí, 1 = no) |
| `$FFFC/$FFFD` | vector RESET (usar `jmp ($FFFC)` para reiniciar) |
| `$D400-$D41F` | SID (voz 3: `$D41B` = OSC3 random) |

### Límites y rangos
| Cosa | Valor |
|---|---|
| Pantalla visible | 40×30 celdas (320×240 px) |
| Mapa de fondo | 64×32 celdas |
| Fuente de texto (tile = ASCII) | `$20-$7F` |
| Tiles propios (libres) | `$01-$1F` |
| Filas de texto seguras | `1..28` |
| Patrones de fondo | 256 |
| Patrones de sprite | 64 |
| Sprites (OAM) | 32 (máx. **8 por línea**) |
| RAM de programa | `$0800-$3DFF` (~13.5 KB) |
