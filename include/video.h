/**
 * ============================================================================
 * video.h - Biblioteca del Core de Vídeo para el Monitor 6502 / Tang Nano 9K
 * ============================================================================
 * Modelo: coprocesador gráfico estilo NES/VIC-II (tiles + sprites, SIN
 * framebuffer). El CPU no dibuja píxeles: escribe VRAM y registros.
 *
 * Comunicación con el core:
 *   - Puerto indirecto de 3 registros para memoria (VRAM/OAM/patrones):
 *         $D800 = dirección baja
 *         $D801 = área + dirección alta   (ver encoding abajo)
 *         $D802 = dato  (escribirlo DISPARA la escritura)
 *   - Registros directos para scroll, split de raster y status.
 *
 * IMPORTANTE:
 *   - Toda escritura a OAM/VRAM/scroll debe hacerse en VBLANK (vc_wait_vblank).
 *   - Al arrancar, esperar VIDEO_READY antes de tocar la VRAM (vc_wait_ready).
 *   - El puerto indirecto NO auto-incrementa: hay que reescribir $D800/$D801
 *     antes de cada $D802. Las funciones de esta librería lo hacen por ti.
 *
 * Referencia: 07-MANUAL-PROGRAMACION.md
 * ============================================================================
 */

#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

/* ===========================================================================
 * 1. REGISTROS DEL CORE (acceso directo)
 * =========================================================================== */

#define VID_ADDR_LO   (*(volatile uint8_t *)0xD800)  /* W: dir baja VRAM     */
#define VID_ADDR_HI   (*(volatile uint8_t *)0xD801)  /* W: área + dir alta   */
#define VID_DATA      (*(volatile uint8_t *)0xD802)  /* W: dato (dispara)    */
#define VID_STATUS    (*(volatile uint8_t *)0xD803)  /* R: STATUS            */
#define VID_SCROLL_X_LO (*(volatile uint8_t *)0xD804) /* W: scroll X lo      */
#define VID_SCROLL_X_HI (*(volatile uint8_t *)0xD805) /* W: scroll X bits 2:0*/
#define VID_SCROLL_Y_LO (*(volatile uint8_t *)0xD806) /* W: scroll Y lo      */
#define VID_SCROLL_Y_HI (*(volatile uint8_t *)0xD807) /* W: scroll Y bits 2:0*/
#define VID_MAP_STRIDE  (*(volatile uint8_t *)0xD808) /* W: stride (fijo 64) */
#define VID_RASTER_LINE0 (*(volatile uint8_t *)0xD809)/* W: fin banda sup    */
#define VID_BAND2_X_LO  (*(volatile uint8_t *)0xD80A) /* W: scroll X banda 2 */
#define VID_BAND2_X_HI  (*(volatile uint8_t *)0xD80B)
#define VID_BAND2_Y_LO  (*(volatile uint8_t *)0xD80C) /* W: scroll Y banda 2 */
#define VID_BAND2_Y_HI  (*(volatile uint8_t *)0xD80D)
#define VID_RASTER_LINE1 (*(volatile uint8_t *)0xD80E)/* W: fin banda media  */
#define VID_BAND3_X_LO  (*(volatile uint8_t *)0xD80F) /* W: scroll X banda 3 */
#define VID_BAND3_X_HI  (*(volatile uint8_t *)0xD810)
#define VID_BAND3_Y_LO  (*(volatile uint8_t *)0xD811) /* W: scroll Y banda 3 */
#define VID_BAND3_Y_HI  (*(volatile uint8_t *)0xD812)
#define VID_PAL_PTR     (*(volatile uint8_t *)0xD813) /* W: entrada de paleta */
#define VID_PAL_LO      (*(volatile uint8_t *)0xD814) /* W: color bits 7:0   */
#define VID_PAL_HI      (*(volatile uint8_t *)0xD815) /* W: bits 11:8 + dispara+inc */
#define VID_SETUP       (*(volatile uint8_t *)0xD816) /* W: dispara setup de VRAM */
#define VID_SETUP_ST    (*(volatile uint8_t *)0xD817) /* R: estado del setup      */

/* ===========================================================================
 * 2. BITS DE STATUS ($D803)
 * =========================================================================== */

#define VC_STATUS_VBLANK      0x80  /* 1 = fuera de zona visible (seguro)    */
#define VC_STATUS_OVERFLOW    0x40  /* 1 = >8 sprites en una línea            */
#define VC_STATUS_SOLID_HIT   0x20  /* 1 = algún sprite tocó tile sólido      */
#define VC_STATUS_VIDEO_READY 0x10  /* 1 = init de VRAM terminada             */

/* Bits de $D817 (SETUP_ST) */
#define VC_SETUP_BUSY         0x01  /* 1 = setup de VRAM en curso             */

/* ===========================================================================
 * 3. ÁREAS DEL PUERTO INDIRECTO ($D801)
 * =========================================================================== */

#define VC_AREA_TILEMAP       0x00  /* tilemap        (dir 0..2047)           */
#define VC_AREA_ATTR          0x40  /* atributos      (dir 0..2047)           */
#define VC_AREA_PAT_BG0       0x80  /* patrón fondo plano 0 (tile*8+fila)     */
#define VC_AREA_PAT_BG1       0xA0  /* patrón fondo plano 1                   */
#define VC_AREA_OAM           0xC0  /* OAM            (dir 0..159)            */
#define VC_AREA_PAT_SPR0      0xC8  /* patrón sprite plano 0 (patron*8+fila)  */
#define VC_AREA_PAT_SPR1      0xE8  /* patrón sprite plano 1                  */

/* ===========================================================================
 * 4. GEOMETRÍA / LÍMITES
 * =========================================================================== */

#define VC_SCREEN_W           320   /* píxeles lógicos                        */
#define VC_SCREEN_H           240
#define VC_SCREEN_COLS        40    /* tiles visibles                         */
#define VC_SCREEN_ROWS        30
#define VC_MAP_COLS           64    /* mapa de fondo                          */
#define VC_MAP_ROWS           32
#define VC_TILE_SIZE          8
#define VC_NUM_TILES          256   /* patrones de fondo                      */
#define VC_NUM_SPRITE_PAT     64    /* patrones de sprite utilizables (0..63) */
#define VC_NUM_SPRITES        32    /* slots de OAM                           */
#define VC_SPRITES_PER_LINE   8     /* máximo por línea (OVERFLOW si se pasa) */
#define VC_MIN_SPACE          0x20  /* fuente ASCII $20-$7F ocupa estos tiles */

/* Tile vacío: color 0 del fondo = transparente (se ve BG_COLOR).
 * Su patrón debe estar a 0 (lo garantiza vc_clear_bg_patterns/vc_clear_vram). */
#define VC_TILE_BLANK         0x00
#define VC_CHAR_SPACE         0x20  /* espacio en la fuente ASCII */

/* ===========================================================================
 * 5. PALETAS
 * =========================================================================== */

/* Paletas de FONDO: índice = paleta(3:2) + color(1:0 del patrón).
 * Color 0 del fondo = transparente (se ve BG_COLOR).
 * Color 3 = tinta de la fuente de texto.
 *
 * Nombres NEUTROS (VC_BGPAL_0..3): la paleta es reprogramable, así que su color
 * NO es fijo. Los valores por defecto (al arrancar) son:
 *   0: — / azul / cian / blanco      (texto / cielo)
 *   1: — / marrón / gris / blanco    (terreno)
 *   2: — / verde / verde osc / verde (vegetación)
 *   3: — / gris / marrón / verde     (entrada 15 = BG_COLOR)
 * Si reescribes una paleta (§4.4), estos colores cambian. */
#define VC_BGPAL_0            0     /* preset: azul / cian / blanco           */
#define VC_BGPAL_1            1     /* preset: marrón / gris / blanco         */
#define VC_BGPAL_2            2     /* preset: verde / verde osc / verde      */
#define VC_BGPAL_3            3     /* preset: gris / marrón / verde          */

/* --- Colores por nombre: ÍNDICE dentro de la paleta (0-3) ---
 * El COLOR de un píxel lo determina el PATRÓN del tile, no el atributo de la
 * celda. Estos índices son los bits 1:0 del byte del patrón. El color real que
 * se ve depende de la PALETA elegida para la celda (que es reprogramable).
 *
 *   idx  (los colores de abajo son los PRESET por defecto)
 *   ---  ----------------------------------------------
 *   0    siempre transparente
 *   1    color de la paleta en la posición 1
 *   2    color de la paleta en la posición 2
 *   3    color de la paleta en la posición 3 (tinta de la fuente)
 */
#define VC_COLOR0             0     /* siempre transparente en el fondo       */
#define VC_COLOR1             1
#define VC_COLOR2             2
#define VC_COLOR3             3     /* color que usa la fuente de texto       */

/* Tinta de texto: la fuente pinta SIEMPRE el color 3 (VC_COLOR3), así que el
 * color de la letra lo elige la PALETA de la celda, no un índice de color.
 * VC_TINTA(paleta) devuelve la paleta que se pasa a vc_put_str_pal/etc. */
#define VC_TINTA(paleta)      ((uint8_t)(paleta))   /* paleta = VC_BGPAL_0..3 */

/* Paletas de SPRITE: color 0 = transparente. Nombres NEUTROS (VC_SPPAL_0..3):
 * la paleta es reprogramable. Colores por defecto (ver manual §4.2):
 *   0: — / piel #FF8800 / marrón #884400 / negro #000000
 *   1: — / azul #0000FF / cian  #00FFFF / blanco #FFFFFF
 *   2: — / magenta #FF00FF / rojo #FF0000 / blanco #FFFFFF
 *   3: — / verde #00FF00 / naranja #FF8800 / blanco #FFFFFF
 */
#define VC_SPPAL_0            0     /* preset: piel / marrón / negro          */
#define VC_SPPAL_1            1     /* preset: azul / cian / blanco           */
#define VC_SPPAL_2            2     /* preset: magenta / rojo / blanco        */
#define VC_SPPAL_3            3     /* preset: verde / naranja / blanco       */

/* Los colores de sprite usan los mismos índices VC_COLOR0..3 dentro de la paleta
 * elegida en FLAGS. Ej.: sprite con paleta VC_SPPAL_1 y patrón de color 3 =
 * blanco (con el preset por defecto). */

/* --- Paletas PROGRAMABLES ($D813-$D815) ---
 * Por defecto valen lo de arriba, pero el CPU puede REESCRIBIR cualquier entrada
 * en cualquier momento (RGB444, con auto-incremento). Útil para fundidos,
 * parpadeos, paletas por nivel, o tu propia paleta.
 * Entradas: 0-15 fondo, 16-31 sprite. Entrada = paleta*4 + color. */

/* Color RGB444 desde componentes 0-15 (un nibble por canal). */
#define VC_RGB444(r,g,b)   ((uint16_t)((((r) & 0x0F) << 8) | \
                                        (((g) & 0x0F) << 4) | \
                                         ((b) & 0x0F)))

/* Convierte un RGB888 (0xRRGGBB) a RGB444 tomando el nibble alto de cada canal. */
#define VC_RGB888(hex)     ((uint16_t)((((hex) >> 12) & 0x0F00) | \
                                        (((hex) >> 8)  & 0x00F0) | \
                                        (((hex) >> 4)  & 0x000F)))

/* Entrada de paleta (para las funciones vc_pal_*). */
#define VC_PAL_ENTRY(paleta, color)  ((uint8_t)((paleta) * 4 + (color)))
#define VC_PAL_BG(paleta, color)     ((uint8_t)(VC_PAL_ENTRY(paleta, color)))
#define VC_PAL_SPR(paleta, color)    ((uint8_t)(16 + VC_PAL_ENTRY(paleta, color)))

/* Índices de paleta por defecto (ver las tablas de arriba). */
#define VC_PAL_BG_COUNT       16    /* 4 paletas x 4 colores (fondo)          */
#define VC_PAL_SPR_BASE       16    /* primera entrada de sprite              */

/* --- BG_COLOR: color de fondo global (manual §4.3) ---
 * Es lo que se ve en el margen de la pantalla y en el fondo vacio (donde el
 * tile tiene color 0 y no hay sprite). NO tiene registro propio: es la
 * entrada 15 del banco de FONDO (paleta 3, color 3). Al cambiar la entrada 15
 * tambien cambia el color 3 de la paleta 3 (comparten entrada, ver manual). */
#define VC_PAL_BGCOLOR        15    /* entrada de BG_COLOR (= VC_PAL_BG(3,3)) */
#define VC_BG_COLOR_DEFAULT   0x48C /* azul cielo por defecto                 */

/* Fija el color de fondo global (BG_COLOR) con un RGB444. */
void     vc_set_bgcolor(uint16_t rgb444);

/* ===========================================================================
 * 6. ATRIBUTO DEL FONDO (bytes de $D801 área ATTR)
 * =========================================================================== */

#define VC_ATTR_PRIO          0x80  /* sprite/tile detrás del fondo           */
#define VC_ATTR_FLIP_Y        0x40
#define VC_ATTR_FLIP_X        0x20
#define VC_ATTR_SOLID         0x10  /* celda sólida (colisión)                */
/* bits 3:0 = paleta */

/* ===========================================================================
 * 7. FLAGS DE SPRITE (campo +3 del OAM)
 * =========================================================================== */

#define VC_SPR_FLIP_Y         0x80
#define VC_SPR_FLIP_X         0x40
#define VC_SPR_PRIO           0x20  /* 1 = detrás del fondo                   */
#define VC_SPR_SCALE2X        0x10  /* dibuja al doble (16x16 en pantalla)    */
#define VC_SPR_XBIT8          0x04  /* bit 8 de la coordenada X (X de 9 bits) */
/* bits 1:0 = paleta */

/* Y >= este valor deshabilita el sprite */
#define VC_SPR_DISABLED       248

/* Campos del OAM (offset dentro del sprite) */
#define VC_OAM_X              0
#define VC_OAM_Y              1
#define VC_OAM_TILE           2
#define VC_OAM_FLAGS          3
#define VC_OAM_COLL           4

/* Puntos de colisión predefinidos (para COLL_POINT, offset 0-7) */
#define VC_COLL_CENTER        0x24  /* dx=4, dy=4  → centro                   */
#define VC_COLL_FEET          0x3C  /* dx=4, dy=7  → pie                      */
#define VC_COLL_HEAD          0x04  /* dx=4, dy=0  → cabeza                   */
#define VC_COLL_LEFT          0x20  /* dx=0, dy=4  → borde izquierdo          */
#define VC_COLL_RIGHT         0x27  /* dx=7, dy=4  → borde derecho            */
#define VC_COLL_POINT(dx, dy) ((((dy) & 7) << 3) | ((dx) & 7))

/* ===========================================================================
 * 8. NÚCLEO EN ENSAMBLADOR (src/video.s)
 * ===========================================================================
 * Primitivas de bajo nivel. Son las funciones rápidas de acceso a VRAM/OAM.
 * =========================================================================== */

/* --- Sincronización --- */
void     vc_wait_ready(void);       /* espera VIDEO_READY al arrancar        */
void     vc_wait_vblank(void);      /* espera ENTRAR en VBLANK               */
void     vc_wait_vblank_end(void);  /* espera SALIR de VBLANK                */
uint8_t  vc_status(void);           /* lee STATUS ($D803)                    */

/* --- Setup de VRAM por hardware ($D816/$D817) ---
 * Dispara el setup del core: limpia tilemap/atributos/patrones y re-expande la
 * fuente (~120-275 us). vc_clear_vram() lo usa internamente. */
void     vc_wait_setup(void);       /* espera a que el setup termine         */

/* --- Escritura indirecta rápida (no auto-incrementa) --- */
void     vc_write(uint8_t area, uint16_t addr, uint8_t data);

/* --- Tilemap y atributos --- */
void     vc_put_cell(uint8_t col, uint8_t row, uint8_t tile);
void     vc_put_attr(uint8_t col, uint8_t row, uint8_t attr);

/* --- Patrones --- */
void     vc_load_bg_pattern(uint8_t tile, const uint8_t *plan0, const uint8_t *plan1);
void     vc_load_spr_pattern(uint8_t spr, const uint8_t *plan0, const uint8_t *plan1);

/* --- OAM --- */
void     vc_oam_put(uint8_t spr, uint8_t field, uint8_t data);

/* --- Volcados masivos (rápidos; las versiones sin sufijo no tocan el área) --- */
void     vc_fill_tilemap(uint8_t tile);                    /* 2048 celdas     */
void     vc_clear_attr(void);                              /* 2048 attrs a 0  */
void     vc_clear_bg_patterns(void);                       /* 256 patrones bg */
void     vc_clear_spr_patterns(void);                      /* 64 patrones de sprite */
void     vc_copy_tilemap(const uint8_t *src);              /* 2048 bytes      */
void     vc_copy_attr(const uint8_t *src);                 /* 2048 bytes      */
void     vc_clear_oam(void);                               /* Y=disabled (todas) */

/* --- Scroll --- */
void     vc_set_scroll_x(uint16_t x);
void     vc_set_scroll_y(uint16_t y);

/* --- Split de raster (bandas) --- */
void     vc_set_raster(uint8_t line0, uint8_t line1);      /* 0xFF = sin banda */
void     vc_set_band2_scroll(uint16_t x, uint16_t y);
void     vc_set_band3_scroll(uint16_t x, uint16_t y);

/* --- Paletas programables ($D813-$D815) ---
 * Fija el puntero de entrada (0-15 fondo, 16-31 sprite).
 * Escríbelo una vez y luego usa vc_pal_set/vc_pal_load (auto-incrementan). */
void     vc_pal_ptr(uint8_t entrada);

/* Escribe UNA entrada con un color RGB444 (auto-incrementa el puntero). */
void     vc_pal_set(uint8_t entrada, uint16_t rgb444);

/* Carga 'count' colores RGB444 consecutivos desde un array (auto-incrementa). */
void     vc_pal_load(uint8_t entrada, const uint16_t *colores, uint8_t count);

/* --- Helpers de paleta (src/gfx.c): por (paleta, color) o paleta completa --- */
void     vc_pal_set_bg(uint8_t paleta, uint8_t color, uint16_t rgb444);
void     vc_pal_set_spr(uint8_t paleta, uint8_t color, uint16_t rgb444);
void     vc_pal_load_bg(uint8_t paleta, const uint16_t *colores4);
void     vc_pal_load_spr(uint8_t paleta, const uint16_t *colores4);

/* ===========================================================================
 * 9. CAPA DE ALTO NIVEL EN C (src/gfx.c)
 * =========================================================================== */

/* --- Atributos por paleta/flags (wrapper cómodo) --- */
void     vc_set_cell_attr(uint8_t col, uint8_t row, uint8_t paleta,
                         uint8_t flags);          /* flags: VC_ATTR_*        */
void     vc_free_cell(uint8_t col, uint8_t row);  /* tile 0, attr 0          */

/* --- Sprites --- */
typedef struct {
    uint8_t  x_lo;      /* X bits 7:0                                                */
    uint8_t  y;         /* Y (>= VC_SPR_DISABLED deshabilita)                        */
    uint8_t  tile;      /* patrón 0-63                                               */
    uint8_t  flags;     /* VC_SPR_* | paleta                                         */
    uint8_t  coll;      /* VC_COLL_*                                                 */
} vc_sprite_t;

void     vc_sprite_set(uint8_t spr, const vc_sprite_t *s);
void     vc_sprite_move(uint8_t spr, uint16_t x, uint8_t y, uint8_t base_flags);
void     vc_sprite_disable(uint8_t spr);
void     vc_sprite16_set(uint8_t first, uint16_t x, uint8_t y,
                         uint8_t tile0, uint8_t flags); /* objeto 16x16 (4 sprites) */

/* --- Colisión --- */
uint8_t  vc_solid_hit(void);                        /* flag global            */

/* Cajas AABB (núcleo en ensamblador, src/collide.s). */
typedef struct {
    uint16_t x, y;      /* esquina superior izquierda (X 9 bits, Y 8 bits)   */
    uint16_t w, h;      /* ancho y alto en píxeles lógicos                   */
} vc_box_t;

/* 1 = la caja A solapa con la caja B. */
uint8_t  vc_box_overlap(const vc_box_t *a, const vc_box_t *b);

/* 1 = el punto (px,py) cae dentro de la caja b. */
uint8_t  vc_box_contains(const vc_box_t *b, uint16_t px, uint16_t py);

/* Construye una caja desde un sprite 8x8 en (x,y) (o 16x16/32x32). */
void     vc_box_from_sprite(vc_box_t *box, uint16_t x, uint16_t y,
                            uint8_t size);          /* size = 8, 16 o 32 */

/* --- Texto (modo texto: tile = ASCII, paleta 0, color 3 = tinta) --- */
void     vc_text_init(void);                        /* limpia 40x30 con espacio*/
void     vc_put_char(uint8_t col, uint8_t row, char c);
void     vc_put_str(uint8_t col, uint8_t row, const char *s);
void     vc_put_str_pal(uint8_t col, uint8_t row, const char *s, uint8_t paleta);

/* --- Fondo: carga de tileset y volcado de pantalla --- */
void     vc_clear_vram(void);                       /* limpia TODA la VRAM     */
void     vc_load_tiles(uint8_t tile_base, uint8_t count,
                       const uint8_t *pat0, const uint8_t *pat1);
void     vc_blit_screen(const uint8_t *src);        /* copia 40x30 = 1200 B    */

/* ===========================================================================
 * 10. MACROS DE CONVENIENCIA (rápidas, sin llamada)
 * =========================================================================== */

/* Escribe un dato en una dirección de VRAM ya conocida (2 accesos + disparo) */
#define VC_WRITE(area, addr, data) do {          \
    VID_ADDR_LO = (uint8_t)(addr);               \
    VID_ADDR_HI = (uint8_t)((area)) | (uint8_t)(((addr) >> 8) & 0x07); \
    VID_DATA    = (data);                        \
} while (0)

/* Celda del tilemap: fila*64 + col */
#define VC_CELL(col, row) ((uint16_t)((row) * VC_MAP_COLS + (col)))

/* Dirección de patrón de fondo: tile*8 + fila */
#define VC_BG_PAT_ADDR(tile, fila) ((uint16_t)((tile) * 8 + (fila)))

/* Dirección de patrón de sprite: patron*8 + fila (patron = índice 0..63) */
#define VC_SPR_PAT_ADDR(patron, fila) ((uint16_t)((patron) * 8 + (fila)))

/* Byte de OAM: sprite*5 + campo */
#define VC_OAM_ADDR(spr, field) ((uint16_t)((spr) * 5 + (field)))

#endif /* VIDEO_H */
