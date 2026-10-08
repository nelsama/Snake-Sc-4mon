# Bug (RESUELTO): píxel fantasma en el borde derecho de un sprite (video_core)

> ## ✅ Estado: RESUELTO en el core (hardware)
>
> El bug se corrigió en `src/hdmi/video_core.vhd`. Ya **no** hace falta el
> workaround de software: los patrones de sprite pueden usar la columna 7 con
> normalidad.
>
> **Qué se cambió:** se eliminó la **etapa B redundante** del pipeline de sprites
> (`spr_active1` / `spr_pixcode1`). El mux final ahora usa la salida de la **etapa A**
> (`spr_active` / `spr_pixcode`), que queda en **N+2**, igual que el fondo. Antes el
> sprite salía en **N+3** (1 px de retraso respecto al fondo) → píxel fantasma en el
> borde derecho. `spr_pal_b` y `spr_prio2` ya llegaban a N+2, así que quedaron
alineados.
>
> Este documento se conserva como registro del diagnóstico y de la reproducción,
> por si el síntoma reaparece en una versión futura del core.

---

## Síntoma

Un sprite 8×8 con **contenido en su última columna** (columna 7, bit 0 de cada
byte de patrón) muestra **una línea de 1 píxel** justo **a su derecha**.

Propiedades observadas:

- Aparece **solo con sprites**. Los patrones de **tile de fondo** no se ven
  afectados.
- Es **independiente del flip X/Y**: el píxel se queda fijo a la derecha aunque
  se voltee el sprite (lo que demuestra que no proviene del dibujo, sino de la
  posición/índice).
- Al **poner a transparente la columna 7** del patrón, la línea **desaparece**.
  Es decir: el píxel fantasma está dibujando **contenido de patrón de una columna
  fuera de rango** (reciclada).

## Reproducción mínima

1. Cargar un patrón de sprite 8×8 con contenido en la columna 7, por ejemplo
   (1 = píxel visible, · = transparente):

   ```
   ..#####.
   ####...#
   ###..@.#
   ####...#
   ####...#
   ###..@.#
   ####...#
   ..#####.
   ```

   (la columna 7 del medio es contenido sólido)

2. Dibujar **un único sprite** en la pantalla con X = `col*8` (col cualquiera),
   Y cualquiera, `FLAGS` con `FLIP_X` y `FLIP_Y` a 0, paleta cualquiera, 1x
   (sin SCALE2X).

3. **Resultado:** aparece una línea vertical de 1 píxel de ancho a la derecha del
   sprite.

4. **Prueba de la hipótesis:** repetir con la **columna 7 del patrón en
   transparente**. La línea desaparece.

5. **Prueba del flip:** dibujar el mismo sprite con `FLIP_X = 1`. La línea **sigue
   a la derecha** (no se mueve al lado izquierdo). Con `FLIP_X = 1` y la columna 7
   transparente, el sprite volteado **tampoco** muestra línea.

## Causa confirmada (video_core.vhd)

**Causa real:** desalineación de 1 píxel en el pipeline de sprites respecto al del
fondo.

El pipeline de sprites tenía una **etapa B redundante** (`spr_active1` /
`spr_pixcode1`), de modo que el píxel del sprite salía en **N+3** mientras que el
fondo sale en **N+2**. Ese píxel de retraso hacía que el sprite se dibujara 1 píxel
corrido y, en el borde derecho, apareciera un píxel fantasma. Al ser un desfase de
**posición** (no del dibujo), el defecto era **independiente del flip** y solo
afectaba a sprites (el fondo no pasa por esa etapa).

> Diagnóstico inicial (descartado como causa raíz, se deja como referencia): se
> sospechó del índice horizontal de 3 bits `idx := to_integer(sx(2 downto 0))`, que
> al reciclarse podía leer una columna fuera de rango. La causa real resultó ser la
> etapa B redundante del pipeline.

### Referencia: el fix parcial documentado por el propio autor

El archivo ya documentaba un artefacto similar y una corrección parcial
(líneas 344-347), que resolvía el caso **entre sprites compuestos** pero no el
borde de un sprite individual:

```
-- Version RETRASADA 1 ciclo de la seleccion, para alinear con spr_pat_data
-- (la BSRAM de patrones tiene 1 ciclo de latencia; sin este retardo, en el
--  borde entre dos sprites distintos se mezcla el patron de uno con la X
--  del otro -> linea de 1 pixel entre sprites compuestos).
```

## Fix aplicado **en el core**

Se **eliminó la etapa B redundante** del pipeline de sprites (`spr_active1` /
`spr_pixcode1`). El mux final ahora usa la salida de la **etapa A**
(`spr_active` / `spr_pixcode`), que queda en **N+2**, igual que el fondo.
`spr_pal_b` y `spr_prio2` ya llegaban a N+2, así que quedaron alineados sin
cambios.

Resultado: desaparece el píxel fantasma del borde derecho; los patrones de sprite
pueden usar la **columna 7** con normalidad.

### Nota histórica: workaround de software (YA RETIRADO)

Mientras el core no estaba corregido, en el juego se aplicó un parche temporal:
dejar a transparente la **columna 7** de los patrones de la cabeza
(`F1→F0`, `81→80`, `A5→A4`, `DB→DA`, `FF→FE`) para que el píxel fantasma no
dibujara nada. **Ese parche ya se revirtió**: con el core corregido, los patrones
usan su columna 7 original.
