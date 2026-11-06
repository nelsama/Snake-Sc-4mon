/**
 * ============================================================================
 * game.c - SNAKE (la serpiente) para el Core de Video (vc) / Monitor 6502
 * ============================================================================
 * Clasico juego de la serpiente sobre una rejilla de celdas:
 *   - La serpiente avanza sola; con las flechas (o WASD) eliges la direccion.
 *   - Al comer una manzana crece un segmento y ganas puntos.
 *   - Pierdes si chocas contra ti misma o contra las paredes del tablero.
 *
 * TECNICA DE DIBUJO
 *   Cada celda de la serpiente y la manzana son TILES del tilemap (se borran
 *   escribiendo el tile de fondo). En cada paso se tocan 2-3 celdas (nueva
 *   cabeza, cuerpo y cola reciclada): muy barato para el 6502. La cabeza usa un
 *   patron propio de bloque solido para destacar; el cuerpo, la cola y la
 *   manzana usan la letra 'O' de la fuente del sistema.
 *
 * CONTROLES (UART):
 *   w/8 = arriba,  s/2 = abajo,  a/4 = izquierda,  d/6 = derecha  (y cursor si
 *   el terminal envia los codigos ANSI de flechas).
 *   q = salir.  En GAME OVER: r = jugar de nuevo, q = salir.
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "romapi.h"
#include "sound.h"
#include "joy.h"

/* ===========================================================================
 * GEOMETRIA DEL TABLERO
 * ===========================================================================
 * El core dibuja 40x30 celdas visibles. Reservamos una banda superior para el
 * HUD (filas 0-2) y una banda inferior para el marcador (filas 27-29); el
 * tablero de juego queda en filas 3..26 y columnas 0..39.
 *
 * El tablero se rodea de un muro de tiles: la serpiente choca contra los bordes
 * del rectangulo interior (SNAKE_MIN_COL..MAX_COL, MIN_ROW..MAX_ROW).
 * =========================================================================== */
#define BOARD_TOP     3                   /* primera fila del tablero         */
#define BOARD_BOTTOM  (VC_SCREEN_ROWS - 3) /* primera fila del HUD inferior   */
#define BOARD_ROWS    (BOARD_BOTTOM - BOARD_TOP)  /* 23 filas                 */

#define SNAKE_MIN_COL 1
#define SNAKE_MAX_COL (VC_SCREEN_COLS - 2)        /* 38 */
#define SNAKE_MIN_ROW (BOARD_TOP + 1)             /* 4  */
#define SNAKE_MAX_ROW (BOARD_BOTTOM - 2)          /* 25 */
#define BOARD_COLS    (SNAKE_MAX_COL - SNAKE_MIN_COL + 1)   /* 38 */
#define BOARD_HEIGHT  (SNAKE_MAX_ROW - SNAKE_MIN_ROW + 1)   /* 22 */

/* Longitud maxima de la serpiente.
 * El tablero tiene 38x22 = 836 celdas, pero reservar 836 celdas de 2 bytes
 * (1672 B) se come casi toda la RAM libre. En la practica una partida no llega
 * ni de lejos (haria falta comer 830 frutas). Se reserva un tope holgado y, si
 * se alcanza, la partida termina como "victoria" igual que llenar el tablero.
 * 400 celdas = 800 bytes (ahorra ~870 B de BSS). */
#define MAX_SNAKE     100

/* ===========================================================================
 * PATRONES
 * ===========================================================================
 * El core solo tiene 2 bits por pixel (4 colores). Pero necesitamos CINCO
 * estados en el editor (transparente + 3 colores), asi que el modelo es:
 *
 *     1 = transparente (sin pintar)
 *     2 = blanco       -> ojo
 *     3 = azul         -> cuerpo
 *     4 = amarillo     -> pupila
 *
 * Al generar los planos se resta 1 y se usan los 2 bits: 0..3.
 * En la paleta de sprite: 0 = transparente, 1 = blanco, 2 = azul, 3 = amarillo.
 *
 * La CABEZA se dibuja como SPRITE (OAM). El CUERPO y el MURO son tiles de
 * fondo (patrones en $0F y $11).
 * =========================================================================== */

/* Tile de fondo vacio: indice 0 -> transparente -> se ve BG_COLOR (negro). */
static const uint8_t tile_blank_p0[8] = { 0,0,0,0,0,0,0,0 };

/* Muro del tablero: bloque solido (indice 3), patron propio en $0F. */
static const uint8_t tile_wall_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* Bloques solidos del lienzo del editor, uno por color de PAL_CANVAS:
 *   TILE_SOL0 -> color 0 (blanco), TILE_SOL1 -> color 1 (azul),
 *   TILE_SOL2 -> color 2 (amarillo).  El color 3 esta reservado (BG_COLOR). */
/* Bloques solidos del lienzo del editor. Cada uno pinta en un indice DISTINTO
 * de la paleta del lienzo (PAL_CANVAS), asi el color lo decide la paleta:
 *   TILE_SOL0 -> color 0
 *   TILE_SOL1 -> color 1
 *   TILE_SOL2 -> color 2
 * El color 3 no se usa (la paleta 3 es BG_COLOR y no se puede tocar).
 * plano0 marca el bit 0 y plano1 el bit 1 de cada pixel. */
static const uint8_t tile_sol0_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t tile_zero[8]    = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uint8_t tile_ff[8]      = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* Cuerpo de la serpiente (simetrico), tile de fondo en $11. */
static const uint8_t tile_body_p0[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t tile_body_p1[8] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

/* Cabeza de la serpiente: PLANTILLA de fabrica (mirando a la DERECHA).
 * El dibujo editable vive en RAM (face_buf) y se convierte a planos al cargar.
 * Indices del editor: 0 = transparente, 1 = azul (cuerpo),
 *                     2 = amarillo (pupila), 4 = blanco (ojo). */
static const uint8_t face_factory[64] = {
    0,0,1,1,1,1,1,0,
    1,1,1,1,4,4,4,1,
    1,1,1,4,4,2,4,1,
    1,1,1,1,4,4,4,1,
    1,1,1,1,4,4,4,1,
    1,1,1,4,4,2,4,1,
    1,1,1,1,4,4,4,1,
    0,0,1,1,1,1,1,0
};

/* Patron del cursor del editor de cara: marco de color 1 (relleno) y color 2
 * (interior transparente). El color 1 sale de la paleta que se le pasa al
 * sprite, y se alterna entre dos paletas (blanco y negro) para que el marco
 * nunca se pierda sobre una zona blanca del dibujo. */
static const uint8_t cur_p0[8] = { 0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0xFF };

/* --- FRUTAS (tiles de fondo). 3 colores: 1 = rojo, 2 = verde, 3 = negro. --- */
/* Fresa (pepitas negras, hojas verdes). */
static const uint8_t tile_fruit_straw_p0[8] = { 0x00,0x24,0x7E,0xFF,0xFF,0x7E,0x3C,0x18 };
static const uint8_t tile_fruit_straw_p1[8] = { 0x10,0x18,0x08,0x20,0x14,0x00,0x10,0x00 };
/* Manzana (hoja verde). */
static const uint8_t tile_fruit_apple_p0[8] = { 0x00,0x66,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t tile_fruit_apple_p1[8] = { 0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00 };

/* ===========================================================================
 * CONSTANTES DEL JUEGO
 * =========================================================================== */
#define TILE_BLANK    0x00   /* tile transparente (fondo negro)               */
#define TILE_WALL     0x0F   /* muro del tablero (bloque solido)              */
#define TILE_BODY     0x11   /* cuerpo de la serpiente (simetrico)            */
#define TILE_FRUIT_FRESA  0x12   /* fresa   (tile de fondo)                    */
#define TILE_FRUIT_MANZANA 0x13  /* manzana (tile de fondo)                    */
/* Bloques solidos del lienzo del editor, uno por color. */
#define TILE_SOL0     0x1A   /* pixel en color 0 */
#define TILE_SOL1     0x1B   /* pixel en color 1 */
#define TILE_SOL2     0x1C   /* pixel en color 2 */

/* Indices de color del editor de cara (lo que se guarda en FACE.DAT).
 * Son los numeros que el usuario ve en el HUD:
 *     0 = transparente (no pinta)
 *     1 = azul    -> cuerpo
 *     2 = amarillo -> pupila
 *     4 = blanco  -> ojo
 * El valor 3 queda libre. */
#define FC_TRANS     0   /* transparente (no pinta) */
#define FC_AZUL      1   /* cuerpo                  */
#define FC_AMARILLO  2   /* pupila                  */
#define FC_BLANCO    4   /* ojo                     */

/* Indice de color dentro del patron (0..3). Es el valor guardado menos 1,
 * salvo el blanco (4) que ocupa el hueco del 3. */
#define FC_COLOR(v)  ((uint8_t)(((v) == FC_BLANCO) ? 3 : (v)))

/* Sprite de la cabeza. */
#define SPR_HEAD      0      /* slot de OAM de la cabeza                      */
#define SPR_PAT_HEAD_R 0     /* patron de sprite: mira a la DERECHA           */
#define SPR_PAT_HEAD_L 1     /* patron de sprite: mira a la IZQUIERDA         */
#define SPR_PAT_HEAD_U 2     /* patron de sprite: mira ARRIBA                 */
#define SPR_PAT_HEAD_D 3     /* patron de sprite: mira ABAJO                  */

/* Sprite del cursor del editor de cara. */
#define SPR_EDIT_CUR   1     /* slot de OAM del cursor                        */
#define SPR_PAT_CUR    4     /* patron de sprite del marco del cursor         */

/* El marco del cursor alterna entre dos paletas para no perderse nunca.
 * El patron pinta el borde con el COLOR 1 y el interior con el COLOR 2, que
 * es transparente en ambas. Asi el borde pasa de blanco a negro y viceversa:
 * sobre el blanco del ojo se ve el negro y sobre el fondo negro se ve el
 * blanco. Ninguna de las dos paletas toca las de la cabeza/frutas. */
#define PAL_CUR_ON   VC_SPPAL_0   /* cursor visible: borde blanco */
#define PAL_CUR_OFF  VC_SPPAL_1   /* cursor invertido: borde negro */

/* Paletas. La cabeza es sprite -> usa una paleta de SPRITE (VC_SPPAL_*).
 * El cuerpo es tile -> usa una paleta de FONDO (VC_BGPAL_*). */
#define PAL_WALL     VC_BGPAL_0   /* preset: color 3 = blanco      */
#define PAL_BODY     VC_BGPAL_0   /* el cuerpo usa su propio indice (ver setup) */
#define PAL_SNAKE_S  VC_SPPAL_2   /* paleta de sprite de la cabeza */
#define PAL_FRUIT    VC_BGPAL_1   /* paleta de las frutas: 1=rojo, 2=verde, 3=negro */
#define PAL_TEXT     VC_BGPAL_0   /* tinta de texto blanca         */
#define PAL_TITLE    VC_BGPAL_2   /* tinta del banner (verde)      */
/* Paleta del LIENZO del editor. Se usa la paleta 2, de la que solo hace falta
 * el COLOR 3 (tinta verde del texto), asi que los colores 0, 1 y 2 quedan libres:
 *     color 0 = azul, color 1 = amarillo, color 2 = blanco
 * La paleta 3 NO sirve: su color 3 es BG_COLOR (entrada 15) y no se puede tocar. */
#define PAL_CANVAS   VC_BGPAL_2   /* lienzo: color 1 = azul, color 2 = amarillo */
#define PAL_CANVAS_W VC_BGPAL_1   /* lienzo: color 3 = blanco                   */

/* NOTA: la paleta 3 no se usa para texto: su color 3 es BG_COLOR (entrada 15),
 * asi que reescribirla cambiaria el color de fondo. Los subtitulos van en blanco. */

/* ---------------------------------------------------------------------------
 * COLOR DEL CUERPO (elegible)
 * ---------------------------------------------------------------------------
 * El cuerpo lo usan DOS cosas: el tile TILE_BODY (color 1 de PAL_BODY) y el
 * sprite de la cabeza (color 1 de PAL_SNAKE_S). Con una sola escritura en cada
 * una queda todo coherente.
 * El indice va en FACE.DAT (byte FACE_PAL) y se aplica con body_apply(). */
#define BODY_COUNT   4
#define BODY_DEF     0      /* 0 = azul: el de siempre */

/* Velocidad: frames entre pasos. Baja al subir de nivel (mas rapido). */
#define TICK_FRAMES_1 16    /* nivel 1 (arranque lento) */
#define TICK_FRAMES_2 13
#define TICK_FRAMES_3 10
#define TICK_FRAMES_4  7
#define TICK_MIN      5     /* no bajar de aqui */

/* Pausa tras perder antes de mostrar GAME OVER (para ver la colision). */
#define DEATH_PAUSE   26

#define START_LEN     4     /* segmentos iniciales                               */

#define LEVEL_UP_EVERY 5    /* manzanas por nivel */
#define MAX_LEVEL      9

/* Teclas que no distinguen mayus/minus (flechas ANSI). */
#define KEY_UP    0x10
#define KEY_DOWN  0x11
#define KEY_LEFT  0x12
#define KEY_RIGHT 0x13

/* ===========================================================================
 * DIRECCIONES
 * =========================================================================== */
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

static const int8_t dir_dx[4] = {  0,  0, -1,  1 };
static const int8_t dir_dy[4] = { -1,  1,  0,  0 };

/* 1 si las direcciones a y b son opuestas (no se permite girar 180 grados). */
static uint8_t is_opposite(uint8_t a, uint8_t b) {
    return (uint8_t)(((a == DIR_UP)    && (b == DIR_DOWN))  ||
                     ((a == DIR_DOWN)  && (b == DIR_UP))    ||
                     ((a == DIR_LEFT)  && (b == DIR_RIGHT)) ||
                     ((a == DIR_RIGHT) && (b == DIR_LEFT)));
}

/* ===========================================================================
 * ESTADO
 * =========================================================================== */
typedef struct { uint8_t x, y; } cell_t;

static cell_t  snake[MAX_SNAKE];   /* snake[0] = cabeza, [len-1] = cola       */
static uint16_t snake_len;
static uint8_t dir;                /* direccion actual                        */
static uint8_t dir_pending;        /* siguiente direccion (se aplica al tick) */
static cell_t  apple;
static uint8_t  fruit_kind;         /* 0 = fresa, 1 = manzana                  */
static uint16_t score;
static uint8_t  level;
static uint8_t  apples_eaten;
static uint8_t  game_over;
static uint8_t  quit_flag;
static uint8_t  dead;              /* ya choco (pausa de muerte en curso)     */

/* ===========================================================================
 * CARA EDITABLE (datos)
 * ===========================================================================
 * Un unico buffer que es a la vez el contenido de FACE.DAT y la matriz de
 * edicion: [0..1] = magic "FC", [2..65] = 64 celdas (8x8), [66..71] = paleta
 * reservada para la fase 2 (6 bytes, sin usar todavia).
 *
 * Compartir el buffer evita duplicar la cara en RAM: guardar es un
 * rom_mfs_write_via_zp() directo, sin copias.
 * ------------------------------------------------------------------------- */
#define FACE_MAGIC0   'F'
#define FACE_MAGIC1   'C'
#define FACE_DATA     2                    /* offset de la matriz 8x8 */
#define FACE_PAL      66                   /* offset del color de CUERPO */
#define FACE_LEN      72                   /* tamano total del archivo */

/* Acceso a la celda (r,c) de la cara dentro del buffer. */
#define FACE_AT(r,c)  face_buf[FACE_DATA + (r) * 8 + (c)]

static uint8_t face_buf[FACE_LEN];

/* Planos temporales reutilizados para cargar patrones (16 B). */
static uint8_t face_p0[8];
static uint8_t face_p1[8];

/* Mejores puntuaciones de la sesion (top 5), en orden descendente.
 * Se guarda tambien el LARGO (segmentos) con que se logro cada record.
 * Viven en RAM: se pierden al salir al monitor (jmp $8000 reinicia la RAM). */
#define HS_COUNT  5
static uint16_t high_scores[HS_COUNT];
static uint16_t high_lengths[HS_COUNT];
static uint8_t  hs_last_idx;       /* indice del ultimo score insertado (o 0xFF) */

static uint16_t rng_state;

/* ===========================================================================
 * ALEATORIO (xorshift de 16 bits)
 * =========================================================================== */
static uint8_t rng_next(void) {
    rng_state ^= (uint16_t)(rng_state << 7);
    rng_state ^= (uint16_t)(rng_state >> 9);
    rng_state ^= (uint16_t)(rng_state << 8);
    return (uint8_t)(rng_state & 0xFF);
}

/* Siembra el RNG con una mezcla de fuentes no deterministas:
 *   - snd_random(): oscilador de ruido del SID ($D41B, "OSC3 random").
 *   - rom_get_micros(): temporizador real (varia entre partidas y pulsaciones).
 *   - un contador de arranques.
 * Asi la primera fruta y sus posiciones cambian en cada partida. */
static uint8_t rng_boot_count;
static void rng_seed(void) {
    uint32_t t = rom_get_micros();
    uint8_t  s0 = snd_random();   /* primer byte del oscilador de ruido */
    uint8_t  s1 = snd_random();   /* segundo (el LFSR ya avanzo)        */

    rng_boot_count++;
    rng_state  = (uint16_t)((uint16_t)s0 << 8) | s1;
    rng_state ^= (uint16_t)t;
    rng_state ^= (uint16_t)(t >> 16) << 8;
    rng_state ^= (uint16_t)rng_boot_count * 0x9E37u;

    /* Evita el estado cero (xorshift se quedaria atascado). */
    if (rng_state == 0) rng_state = 0xA5A5;

    /* Mezcla inicial: descarta los primeros valores. */
    (void)rng_next();
    (void)rng_next();
}

/* ===========================================================================
 * TEXTO (fuente del sistema: tile = ASCII, tinta = color 3 de la paleta)
 * =========================================================================== */
/* Escribe una cadena con la fuente del sistema en (col,row) y una paleta. NO
 * envuelve: si llega al borde derecho se RECORTA. */
static void put_str_pal(uint8_t col, uint8_t row, const char *s, uint8_t pal) {
    while (*s && col < VC_SCREEN_COLS) {
        vc_put_cell(col, row, (uint8_t)*s);
        vc_set_cell_attr(col, row, pal, 0);
        col++;
        s++;
    }
}

/* Escribe una cadena con la fuente del sistema en (col,row). NO envuelve: si la
 * cadena llega al borde derecho se RECORTA (asi nunca se descuadra el HUD). */
static void put_str_at(uint8_t col, uint8_t row, const char *s) {
    put_str_pal(col, row, s, PAL_TEXT);
}

static void clear_str_at(uint8_t col, uint8_t row, uint8_t len) {
    while (len-- && col < VC_SCREEN_COLS) {
        vc_put_cell(col, row, VC_CHAR_SPACE);
        vc_set_cell_attr(col, row, PAL_TEXT, 0);
        col++;
    }
}

/* ===========================================================================
 * UTILIDADES DEL TABLERO
 * =========================================================================== */

/* Pinta una celda del tablero con un tile, su paleta y flags de atributo. */
static void paint_flags(uint8_t col, uint8_t row, uint8_t tile, uint8_t pal,
                        uint8_t flags) {
    vc_put_cell(col, row, tile);
    vc_set_cell_attr(col, row, pal, flags);
}

/* Pinta una celda del tablero con un tile y su paleta (sin flags). */
static void paint(uint8_t col, uint8_t row, uint8_t tile, uint8_t pal) {
    paint_flags(col, row, tile, pal, 0);
}

/* Limpia toda la pantalla visible (fondo negro). Reutilizado por el banner,
 * el editor y la despedida. */
static void clear_screen(void) {
    uint8_t x, y;
    for (y = 0; y < VC_SCREEN_ROWS; y++)
        for (x = 0; x < VC_SCREEN_COLS; x++)
            paint(x, y, TILE_BLANK, PAL_TEXT);
}

/* Pone el color del CUERPO en las dos paletas que lo usan. Solo 4 colores:
 * el indice va en FACE_PAL y se recorre con una suma sin signo, sin tablas ni
 * modulo (la RAM y el CODE van justos y el 6502 no tiene division). */
/* Pone el color del CUERPO en las dos paletas que lo usan. Solo 4 colores, con
 * un switch: no hay tabla ni division (la RAM y el CODE van justos y el 6502 no
 * tiene instructiva de modulo). */
static void body_apply(void) {
    uint8_t k = face_buf[FACE_PAL];
    uint16_t c;

    if (k >= BODY_COUNT) { k = BODY_DEF; face_buf[FACE_PAL] = k; }
    switch (k) {
        case 1:  c = VC_RGB444(0xF, 0xF, 0xF); break;  /* blanco   */
        case 2:  c = VC_RGB444(0x0, 0xA, 0x0); break;  /* verde    */
        case 3:  c = VC_RGB444(0xF, 0xF, 0x0); break;  /* amarillo */
        default: c = VC_RGB444(0x0, 0x0, 0xA); break;  /* azul     */
    }
    vc_pal_set_bg(PAL_BODY,    1, c);
    vc_pal_set_spr(PAL_SNAKE_S, 1, c);
}

/* Carga en el banco de sprites los DOS patrones que usa la cabeza:
 *   SPR_PAT_HEAD_R : la cara tal cual (mira a la derecha)
 *   SPR_PAT_HEAD_U : la misma cara girada 90 grados (mira arriba)
 * Aprovecha los mismos planos temporales para ambos (ahorra RAM) y calcula el
 * giro directamente desde face_buf, sin matriz temporal (ahorra stack). */
static void face_load_patterns(void) {
    uint8_t r, c;
    uint8_t b0, b1;

    /* --- Patron normal (derecha) ---
     * Los indices del editor van 1..4; el patron usa 0..3, asi que se resta 1.
     * El transparente queda en 0 (sin bits). */
    for (r = 0; r < 8; r++) {
        b0 = 0; b1 = 0;
        for (c = 0; c < 8; c++) {
            uint8_t v = FC_COLOR(FACE_AT(r, c));
            if (v & 1) b0 |= (uint8_t)(0x80 >> c);
            if (v & 2) b1 |= (uint8_t)(0x80 >> c);
        }
        face_p0[r] = b0;
        face_p1[r] = b1;
    }
    vc_load_spr_pattern(SPR_PAT_HEAD_R, face_p0, face_p1);

    /* --- Patron girado 90 grados (mira ARRIBA) ---
     * rot[r][c] = orig[c][7-r].
     * Comprobado con la cara de fabrica (ojos en las filas superiores): asi
     * los ojos quedan ARRIBA (morro hacia arriba). La variante orig[7-c][r]
     * es el giro contrario y dejaba la cara mirando hacia abajo. */
    for (r = 0; r < 8; r++) {
        b0 = 0; b1 = 0;
        for (c = 0; c < 8; c++) {
            uint8_t v = FC_COLOR(FACE_AT(c, (uint8_t)(7 - r)));
            if (v & 1) b0 |= (uint8_t)(0x80 >> c);
            if (v & 2) b1 |= (uint8_t)(0x80 >> c);
        }
        face_p0[r] = b0;
        face_p1[r] = b1;
    }
    vc_load_spr_pattern(SPR_PAT_HEAD_U, face_p0, face_p1);
}

/* Restaura la cara de fabrica en el buffer (magic + dibujo; paleta a 0). */
static void face_default(void) {
    uint8_t i;

    face_buf[0] = FACE_MAGIC0;
    face_buf[1] = FACE_MAGIC1;
    for (i = 0; i < 64; i++) face_buf[FACE_DATA + i] = face_factory[i];
    face_buf[FACE_PAL] = BODY_DEF;              /* color de cuerpo */
    for (i = 1; i < 6; i++) face_buf[FACE_PAL + i] = 0;   /* resto reservado */
}

/* Carga la cara desde FACE.DAT (MicroFS de la ROM API).
 * Devuelve 1 si se cargo una cara valida; 0 si no habia SD/fichero o estaba
 * corrupto (en ese caso deja la de fabrica). */
static uint8_t face_load(void) {
    face_default();

    if (rom_mfs_open("FACE.DAT") != MFS_OK) return 0;
    if (rom_mfs_get_size() != FACE_LEN ||
        rom_mfs_read_via_zp(face_buf, FACE_LEN) != FACE_LEN) {
        rom_mfs_close();
        face_default();
        return 0;
    }
    rom_mfs_close();

    if (face_buf[0] != FACE_MAGIC0 || face_buf[1] != FACE_MAGIC1) {
        face_default();
        return 0;
    }

    /* Version del formato de indices: los ficheros viejos guardaban 0..3
     * (0 = transparente); ahora se guardan 1..4. Un valor 0 delata un fichero
     * antiguo y se descarta, porque se dibujaria con los colores cambiados. */
    {
        uint8_t i;
        for (i = 0; i < 64; i++) {
            uint8_t v = face_buf[FACE_DATA + i];
            if (v != FC_TRANS && v != FC_AZUL && v != FC_AMARILLO && v != FC_BLANCO) {
                face_default();
                return 0;
            }
        }
    }
    return 1;
}

/* Guarda la cara en FACE.DAT. Devuelve 1 si todo fue bien. */
static uint8_t face_save(void) {
    face_buf[0] = FACE_MAGIC0;
    face_buf[1] = FACE_MAGIC1;

    rom_mfs_delete("FACE.DAT");    /* create no sobrescribe */
    if (rom_mfs_create_via_zp("FACE.DAT", FACE_LEN) != MFS_OK) return 0;
    if (rom_mfs_write_via_zp(face_buf, FACE_LEN) != FACE_LEN) {
        rom_mfs_close();
        return 0;
    }
    rom_mfs_close();
    return 1;
}

/* Coloca el SPRITE de la cabeza en la celda (col,row).
 * Hay sprite propio para DERECHA y ARRIBA; IZQUIERDA y ABAJO se obtienen con el
 * flip correspondiente (espejo real de los anteriores). */
static void head_place(uint8_t col, uint8_t row, uint8_t d) {
    uint16_t x = (uint16_t)(col * VC_TILE_SIZE);
    uint8_t  y = (uint8_t)(row * VC_TILE_SIZE);
    uint8_t  fl = PAL_SNAKE_S;   /* paleta del sprite */
    uint8_t  pat;

    if (d == DIR_LEFT) {
        pat = SPR_PAT_HEAD_R;
        fl |= VC_SPR_FLIP_X;
    } else if (d == DIR_RIGHT) {
        pat = SPR_PAT_HEAD_R;
    } else if (d == DIR_UP) {
        pat = SPR_PAT_HEAD_U;
    } else { /* DIR_DOWN */
        pat = SPR_PAT_HEAD_U;
        fl |= VC_SPR_FLIP_Y;
    }

    vc_sprite_move(SPR_HEAD, x, y, fl);
    vc_oam_put(SPR_HEAD, VC_OAM_TILE, pat);
}

/* Apaga el sprite de la cabeza. */
static void head_hide(void) {
    vc_sprite_disable(SPR_HEAD);
}

/* Dibuja el rectangulo de muros y deja el interior vacio. */
static void board_draw(void) {
    uint8_t x, y;

    for (y = 0; y < BOARD_ROWS; y++) {
        uint8_t row = (uint8_t)(BOARD_TOP + y);
        for (x = 0; x < VC_SCREEN_COLS; x++) {
            if (y == 0 || y == (BOARD_ROWS - 1) ||
                x == 0 || x == (VC_SCREEN_COLS - 1)) {
                paint(x, row, TILE_WALL, PAL_WALL);
            } else {
                paint(x, row, TILE_BLANK, PAL_TEXT);
            }
        }
    }
    /* Banda superior e inferior (HUD / marcador). */
    for (y = 0; y < VC_SCREEN_ROWS; y++) {
        if (y >= BOARD_TOP && y < BOARD_BOTTOM) continue;
        for (x = 0; x < VC_SCREEN_COLS; x++) {
            paint(x, y, TILE_BLANK, PAL_TEXT);
        }
    }
}

/* Devuelve 1 si la celda (x,y) esta ocupada por un segmento de la serpiente en
 * los indices [from, to). Para "cuerpo completo" usa from=0, to=snake_len. */
static uint8_t body_has(uint8_t x, uint8_t y, uint16_t from, uint16_t to) {
    uint16_t i;
    for (i = from; i < to; i++) {
        if (snake[i].x == x && snake[i].y == y) return 1;
    }
    return 0;
}

/* Tile de fondo de la fruta actual (fresa o manzana). */
static uint8_t fruit_tile(void) {
    return (uint8_t)(fruit_kind ? TILE_FRUIT_MANZANA : TILE_FRUIT_FRESA);
}

/* Coloca la fruta en una celda libre al azar. Da un numero FINITO de
 * intentos y, si falla, hace un barrido lineal (nunca se cuelga). */
static void apple_place(void) {
    uint8_t tries;
    uint16_t start;

    /* Intento aleatorio: eficiente cuando hay sitio de sobra. */
    for (tries = 0; tries < 24; tries++) {
        uint8_t x = (uint8_t)(SNAKE_MIN_COL + (rng_next() % BOARD_COLS));
        uint8_t y = (uint8_t)(SNAKE_MIN_ROW + (rng_next() % BOARD_HEIGHT));
        if (!body_has(x, y, 0, snake_len)) {
            apple.x = x;
            apple.y = y;
            paint(x, y, fruit_tile(), PAL_FRUIT);
            return;
        }
    }

    /* Barrido lineal desde una posicion aleatoria (busca cualquier hueco). */
    start = (uint16_t)(rng_next() * 7u) % MAX_SNAKE;
    {
        uint16_t k;
        for (k = 0; k < MAX_SNAKE; k++) {
            uint16_t idx = (uint16_t)((start + k) % MAX_SNAKE);
            uint8_t x = (uint8_t)(SNAKE_MIN_COL + (idx % BOARD_COLS));
            uint8_t y = (uint8_t)(SNAKE_MIN_ROW + (idx / BOARD_COLS));
            if (!body_has(x, y, 0, snake_len)) {
                apple.x = x;
                apple.y = y;
                paint(x, y, fruit_tile(), PAL_FRUIT);
                return;
            }
        }
    }
    /* No queda hueco: tablero completo. */
}

/* ===========================================================================
 * SERPIENTE
 * =========================================================================== */

/* Redibuja toda la serpiente (se usa al empezar/reiniciar).
 * La cabeza es un sprite; el cuerpo son tiles de fondo. La celda de la cabeza
 * se deja vacia (transparente) para que se vea el sprite encima. */
static void snake_draw(void) {
    uint16_t i;
    paint(snake[0].x, snake[0].y, TILE_BLANK, PAL_TEXT);
    head_place(snake[0].x, snake[0].y, dir);
    for (i = 1; i < snake_len; i++) {
        paint(snake[i].x, snake[i].y, TILE_BODY, PAL_BODY);
    }
}

/* Coloca la serpiente inicial en el centro, en horizontal, creciendo a la
 * izquierda, y la dibuja. */
static void snake_reset(void) {
    uint8_t i;
    uint8_t cx = (uint8_t)((SNAKE_MIN_COL + SNAKE_MAX_COL) / 2);
    uint8_t cy = (uint8_t)((SNAKE_MIN_ROW + SNAKE_MAX_ROW) / 2);

    for (i = 0; i < START_LEN; i++) {
        snake[i].x = (uint8_t)(cx - i);
        snake[i].y = cy;
    }
    snake_len   = START_LEN;
    dir         = DIR_RIGHT;
    dir_pending = DIR_RIGHT;

    /* Limpia el tablero y redibuja. */
    board_draw();
    snake_draw();
    apple_place();
}

/* Avanza la serpiente UN paso. Devuelve:
 *   0 = movimiento normal, 1 = comio (crecio), 2 = choque/muerte, 3 = victoria.
 * Si no come, borra del tilemap la celda de la cola que se libera. */
static uint8_t snake_step(void) {
    uint8_t nx = (uint8_t)(snake[0].x + dir_dx[dir]);
    uint8_t ny = (uint8_t)(snake[0].y + dir_dy[dir]);
    uint8_t ate;
    uint16_t i;

    /* Colision con los muros. */
    if (nx < SNAKE_MIN_COL || nx > SNAKE_MAX_COL ||
        ny < SNAKE_MIN_ROW || ny > SNAKE_MAX_ROW) {
        return 2;
    }

    ate = (uint8_t)(nx == apple.x && ny == apple.y);

    /* Colision con el cuerpo: comprobar los segmentos 0..len-1. Si NO come, la
     * cola se mueve este mismo paso, asi que su celda no cuenta como choque
     * (se excluye el ULTIMO segmento). Si come, la cola NO se mueve: cuenta toda. */
    {
        uint16_t to = ate ? snake_len : (uint16_t)(snake_len - 1);
        if (body_has(nx, ny, 0, to)) return 2;
    }

    if (!ate) {
        /* Borra la celda de la cola que se libera (antes de mover el array). */
        paint(snake[snake_len - 1].x, snake[snake_len - 1].y, TILE_BLANK, PAL_TEXT);
    }

    /* Desplaza el cuerpo y coloca la nueva cabeza (si come, crece). */
    if (ate) {
        for (i = snake_len; i > 0; i--) snake[i] = snake[i - 1];
        snake_len++;
    } else {
        for (i = (uint16_t)(snake_len - 1); i > 0; i--) snake[i] = snake[i - 1];
    }
    snake[0].x = nx;
    snake[0].y = ny;

    /* La nueva celda de la cabeza queda VACIA (la ocupa el sprite) y el segundo
     * segmento (que era la cabeza) pasa a cuerpo (tile). */
    paint(nx, ny, TILE_BLANK, PAL_TEXT);
    paint(snake[1].x, snake[1].y, TILE_BODY, PAL_BODY);
    head_place(nx, ny, dir);

    if (ate) {
        score = (uint16_t)(score + (uint16_t)(5 + level));
        apples_eaten++;

        if (apples_eaten >= LEVEL_UP_EVERY && level < MAX_LEVEL) {
            apples_eaten = 0;
            level++;
        }

        if (snake_len >= (uint16_t)MAX_SNAKE) return 3;   /* victoria */
        fruit_kind = (uint8_t)(rng_next() & 1);   /* fruta al azar al comer */
        apple_place();
        return 1;
    }
    return 0;
}

/* ===========================================================================
 * ENTRADA (joystick Atari + teclado UART, en paralelo)
 * ===========================================================================
 * El joystick fija la direccion mientras se mantiene. El teclado puede
 * sobrescribirla (se procesa despues). En diagonales del joystick se da
 * prioridad al eje horizontal para que el control sea predecible.
 * =========================================================================== */
static void read_input(void) {
    char c;

    /* --- Joystick Atari (paralelo al teclado) --- */
    {
        uint8_t j = joy_read();

        if (j & JOY_A_LEFT)       dir_pending = DIR_LEFT;
        else if (j & JOY_A_RIGHT) dir_pending = DIR_RIGHT;
        else if (j & JOY_A_UP)    dir_pending = DIR_UP;
        else if (j & JOY_A_DOWN)  dir_pending = DIR_DOWN;
        /* Snake no usa boton de disparo. */
    }

    /* --- Teclado UART --- */
    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();

        switch (c) {
            case 'w': case 'W': case '8': dir_pending = DIR_UP;    break;
            case 's': case 'S': case '2': dir_pending = DIR_DOWN;  break;
            case 'a': case 'A': case '4': dir_pending = DIR_LEFT;  break;
            case 'd': case 'D': case '6': dir_pending = DIR_RIGHT; break;
            case KEY_UP:    dir_pending = DIR_UP;    break;
            case KEY_DOWN:  dir_pending = DIR_DOWN;  break;
            case KEY_LEFT:  dir_pending = DIR_LEFT;  break;
            case KEY_RIGHT: dir_pending = DIR_RIGHT; break;
            case 'q': case 'Q': quit_flag = 1;       break;
            default: break;
        }
    }
}

/* ===========================================================================
 * HUD
 * =========================================================================== */
static void put_u16_at(uint8_t col, uint8_t row, uint16_t v, uint8_t digits) {
    char buf[6];
    uint8_t i;
    for (i = 0; i < digits; i++) {
        buf[digits - 1 - i] = (char)('0' + (v % 10));
        v = (uint16_t)(v / 10);
    }
    buf[digits] = 0;
    put_str_at(col, row, buf);
}

/* Igual que put_u16_at pero con paleta (para resaltar filas). */
static void put_u16_pal(uint8_t col, uint8_t row, uint16_t v, uint8_t digits,
                        uint8_t pal) {
    char buf[6];
    uint8_t i;
    for (i = 0; i < digits; i++) {
        buf[digits - 1 - i] = (char)('0' + (v % 10));
        v = (uint16_t)(v / 10);
    }
    buf[digits] = 0;
    put_str_pal(col, row, buf, pal);
}

static void hud_draw(void) {
    /* Fila superior: titulo + puntuacion + record + nivel. */
    put_str_at(0, 1, "SNAKE");
    put_str_at(7, 1, "PTS");
    put_u16_at(11, 1, score, 4);
    put_str_at(16, 1, "BEST");
    put_u16_at(21, 1, high_scores[0], 4);
    put_str_at(27, 1, "NIV");
    put_u16_at(31, 1, level, 1);

    /* Fila inferior: controles a la izquierda + longitud a la derecha. */
    put_str_at(0, 27, "MOVER: joy/WASD  q: salir");
    put_str_at(28, 27, "LEN");
    put_u16_at(32, 27, snake_len, 3);
}

/* Refresca la puntuacion en el HUD. "BEST" muestra la mejor puntuacion de la
 * sesion (high_scores[0]); si el score la supera se resalta en verde y se
 * actualiza al vuelo, para ir viendo si la sobrepasamos. */
static void hud_update_score(void) {
    uint8_t rec = (uint8_t)(score > high_scores[0]);

    put_u16_at(11, 1, score, 4);
    put_u16_pal(21, 1, (rec ? score : high_scores[0]), 4,
                rec ? PAL_TITLE : PAL_TEXT);
    put_u16_at(31, 1, level, 1);
}

/* ===========================================================================
 * HIGHSCORE (top 5 en RAM)
 * =========================================================================== */

/* Inicializa la tabla de records a 0 (vacia). */
static void hs_reset(void) {
    uint8_t i;
    for (i = 0; i < HS_COUNT; i++) {
        high_scores[i]  = 0;
        high_lengths[i] = 0;
    }
    hs_last_idx = 0xFF;
}

/* Inserta (v = puntos, len = largo alcanzado) en la tabla (orden descendente por
 * puntos) y desplaza el resto. Guarda en hs_last_idx la posicion final (o 0xFF
 * si no entro en el top 5). No inserta puntuaciones de 0. */
static void hs_insert(uint16_t v, uint16_t len) {
    uint8_t i, pos;

    hs_last_idx = 0xFF;
    if (v == 0) return;   /* no guardamos partidas de 0 puntos */

    for (pos = 0; pos < HS_COUNT; pos++) {
        if (v > high_scores[pos]) break;
    }
    if (pos >= HS_COUNT) return;   /* no entra en el top 5 */

    /* Desplaza hacia abajo desde el final hasta 'pos'. */
    for (i = (uint8_t)(HS_COUNT - 1); i > pos; i--) {
        high_scores[i]  = high_scores[i - 1];
        high_lengths[i] = high_lengths[i - 1];
    }
    high_scores[pos]  = v;
    high_lengths[pos] = len;
    hs_last_idx = pos;
}

/* Dibuja la tabla de records en dos columnas (PTS y LARGO), empezando en 'row':
 *   fila row     : "MEJORES:"
 *   fila row+1   : cabeceras "PTS" / "LARGO"
 *   filas row+2..: las 5 entradas
 *
 * Columnas (relativas a 'col'):
 *   col+0 : indice "1."      col+3 : puntos (4 digitos)
 *   col+9 : largo (3 digitos) */
#define HS_COL_PTS   3
#define HS_COL_LEN   9

static void hs_draw(uint8_t col, uint8_t row) {
    uint8_t i;

    /* Ancho total de la tabla: de col+0 a col+11 (12 columnas). */
    put_str_pal(col, row, "MEJORES:", PAL_TITLE);
    put_str_pal((uint8_t)(col + HS_COL_PTS), (uint8_t)(row + 1), "PTS",   PAL_TITLE);
    put_str_pal((uint8_t)(col + HS_COL_LEN), (uint8_t)(row + 1), "LARGO", PAL_TITLE);
    for (i = 0; i < HS_COUNT; i++) {
        uint8_t r = (uint8_t)(row + 2 + i);
        uint8_t pal = (i == hs_last_idx) ? PAL_TITLE : PAL_TEXT;

        /* "1. 0100   020"  ->  puntos | largo */
        char buf[3];
        buf[0] = (char)('1' + i);
        buf[1] = '.';
        buf[2] = 0;
        put_str_pal(col, r, buf, pal);
        put_u16_pal((uint8_t)(col + HS_COL_PTS), r, high_scores[i], 4, pal);
        put_u16_pal((uint8_t)(col + HS_COL_LEN), r, high_lengths[i], 3, pal);
    }
}

/* ===========================================================================
 * VELOCIDAD SEGUN EL NIVEL
 * =========================================================================== */
static uint8_t tick_frames(void) {
    switch (level) {
        case 1:  return TICK_FRAMES_1;
        case 2:  return TICK_FRAMES_2;
        case 3:  return TICK_FRAMES_3;
        case 4:  return TICK_FRAMES_4;
        default: return TICK_MIN;
    }
}

/* ===========================================================================
 * SETUP DE VIDEO
 * =========================================================================== */
static void setup_video(void) {
    uint8_t x, y;

    vc_wait_ready();
    vc_wait_vblank();
    vc_clear_vram();

    /* FONDO NEGRO: BG_COLOR es la entrada 15 de la paleta de fondo. Poniendolo
     * negro, el tile transparente (indice 0) y los huecos de la fuente se ven
     * sobre negro. Se hace dentro del VBLANK. */
    vc_set_bgcolor(VC_RGB444(0x0, 0x0, 0x0));

    /* La CABEZA es un SPRITE (paleta de sprite de la serpiente): indices
     * 1 = azul oscuro (cuerpo), 2 = blanco (ojo), 3 = negro (pupila).
     * El CUERPO es un TILE (paleta de fondo 0): indice 1 = azul oscuro.
     * Los muros y el texto usan el color 3 de la paleta 0 (blanco). */
    vc_pal_set_bg(PAL_WALL,  3, VC_RGB444(0xF, 0xF, 0xF));  /* blanco (muros/HUD) */
    body_apply();                                          /* azul oscuro (cuerpo) */
    /* Paleta de las frutas (3 colores): 1 = rojo, 2 = verde, 3 = negro. */
    vc_pal_set_bg(PAL_FRUIT, 1, VC_RGB444(0xF, 0x0, 0x0));  /* rojo  */
    vc_pal_set_bg(PAL_FRUIT, 2, VC_RGB444(0x0, 0xF, 0x0));  /* verde */
    vc_pal_set_bg(PAL_FRUIT, 3, VC_RGB444(0x0, 0x0, 0x0));  /* negro */
    /* Paleta del LIENZO del editor (VC_BGPAL_2).
     * OJO: en el FONDO el color 0 es SIEMPRE transparente (se ve BG_COLOR), asi
     * que los colores utiles son 1 y 2 (el 3 es la tinta del texto y no se toca).
     * Hacen falta TRES colores y solo hay dos libres, asi que el blanco se pinta
     * con el color 3 de una paleta aparte (ver canvas_cell).
     *     color 1 = azul, color 2 = amarillo */
    vc_pal_set_bg(PAL_CANVAS, 1, VC_RGB444(0x0, 0x0, 0xA));  /* azul     */
    vc_pal_set_bg(PAL_CANVAS, 2, VC_RGB444(0xF, 0xF, 0x0));  /* amarillo */
    /* Blanco del lienzo: color 3 de la paleta 1 (que no se usa para texto). */
    vc_pal_set_bg(PAL_CANVAS_W, 3, VC_RGB444(0xF, 0xF, 0xF));
    /* Paletas del banner: solo se usa el COLOR 3 (tinta del texto de la fuente).
     * OJO: la paleta 3 NO se toca, porque su color 3 es la entrada 15 = BG_COLOR
     * (comparten entrada); reescribirla cambiaria el color de fondo. */
    vc_pal_set_bg(PAL_TITLE, 3, VC_RGB444(0x0, 0xF, 0x4));  /* verde brillante */

    /* Reafirma el fondo NEGRO al final (entrada 15), por si algo la toco. */
    vc_set_bgcolor(VC_RGB444(0x0, 0x0, 0x0));
    /* Cabeza: color de patron 0 = transparente, 1 = azul (cuerpo),
     * 2 = amarillo (pupila), 3 = blanco (ojo). Coincide con FC_COLOR(). */
    vc_pal_set_spr(PAL_SNAKE_S, 0, VC_RGB444(0x0, 0x0, 0x0)); /* transparente */
    vc_pal_set_spr(PAL_SNAKE_S, 1, VC_RGB444(0x0, 0x0, 0xA)); /* azul (cuerpo) */
    vc_pal_set_spr(PAL_SNAKE_S, 2, VC_RGB444(0xF, 0xF, 0x0)); /* amarillo (pupila) */
    vc_pal_set_spr(PAL_SNAKE_S, 3, VC_RGB444(0xF, 0xF, 0xF)); /* blanco (ojo) */

    /* Patrones: muro, cuerpo y frutas (tiles de fondo) + cabeza (sprites).
     * Solo hay dibujo explicito para DERECHA y ARRIBA; IZQUIERDA y ABAJO se
     * obtienen por flip en tiempo de dibujo (head_place). */
    vc_load_bg_pattern(TILE_WALL, tile_wall_p0, tile_wall_p0);
    vc_load_bg_pattern(TILE_BODY, tile_body_p0, tile_body_p1);
    vc_load_bg_pattern(TILE_FRUIT_FRESA,   tile_fruit_straw_p0, tile_fruit_straw_p1);
    vc_load_bg_pattern(TILE_FRUIT_MANZANA, tile_fruit_apple_p0, tile_fruit_apple_p1);
    /* TILE_SOL0 = color 0 (azul): solo plano0.
     * TILE_SOL1 = color 1 (amarillo): solo plano1.
     * TILE_SOL2 = color 2 (blanco): ambos planos. */
    /* Tiles del lienzo. El color 0 del fondo es transparente, asi que los tres
     * colores utiles son 1, 2 y 3:
     *   TILE_SOL0 -> color 1 (azul del lienzo / cuerpo)
     *   TILE_SOL1 -> color 2 (amarillo)
     *   TILE_SOL2 -> color 3 (blanco) */
    vc_load_bg_pattern(TILE_SOL0, tile_ff, tile_zero);   /* color 1 */
    vc_load_bg_pattern(TILE_SOL1, tile_zero, tile_ff);   /* color 2 */
    vc_load_bg_pattern(TILE_SOL2, tile_ff, tile_ff);     /* color 3 */
    face_load_patterns();   /* cabeza: 2 patrones generados desde face_buf */
    vc_load_spr_pattern(SPR_PAT_CUR, cur_p0, cur_p0);   /* marco del cursor */
    /* Colores del cursor: color 1 = el borde, color 2 = interior transparente
     * (mismo negro que el fondo). Se alterna entre estas dos paletas. */
    vc_pal_set_spr(PAL_CUR_ON,  1, VC_RGB444(0xF, 0xF, 0xF));
    vc_pal_set_spr(PAL_CUR_ON,  2, VC_RGB444(0x0, 0x0, 0x0));
    vc_pal_set_spr(PAL_CUR_OFF, 1, VC_RGB444(0x0, 0x0, 0x0));
    vc_pal_set_spr(PAL_CUR_OFF, 2, VC_RGB444(0x0, 0x0, 0x0));

    /* Asegura que el tile transparente existe (ya viene de clear_vram). */
    vc_load_bg_pattern(TILE_BLANK, tile_blank_p0, tile_blank_p0);

    /* Raster fijo: sin bandas de scroll (todo el mundo en la misma banda). */
    vc_set_raster(0xFF, 0xFF);
    vc_set_band2_scroll(0, 0);
    vc_set_band3_scroll(0, 0);

    /* Limpia HUD/marcador. */
    for (y = 0; y < BOARD_TOP; y++) {
        for (x = 0; x < VC_SCREEN_COLS; x++) paint(x, y, TILE_BLANK, PAL_TEXT);
    }
    for (y = BOARD_BOTTOM; y < VC_SCREEN_ROWS; y++) {
        for (x = 0; x < VC_SCREEN_COLS; x++) paint(x, y, TILE_BLANK, PAL_TEXT);
    }
}

/* ===========================================================================
 * PARTIDA
 * ===========================================================================
 * Juega una partida completa. Devuelve 1 si el jugador perdio (Game Over) o
 * 0 si salio con 'q'.
 * =========================================================================== */
static uint8_t play_game(void) {
    uint8_t tick;

    score         = 0;
    level         = 1;
    apples_eaten  = 0;
    game_over     = 0;
    quit_flag     = 0;
    dead          = 0;

    snd_init();
    rng_seed();                               /* semilla no determinista (usa el SID) */
    fruit_kind    = (uint8_t)(rng_next() & 1);   /* primera fruta al azar */

    vc_wait_vblank();
    snake_reset();
    vc_wait_vblank_end();

    hud_draw();

    tick = tick_frames();

    while (!quit_flag && !game_over) {
        vc_wait_vblank();

        read_input();

        if (--tick == 0) {
            uint8_t r;

            tick = tick_frames();

            /* Aplica la ultima direccion pedida, sin giro de 180 grados. */
            if (!is_opposite(dir, dir_pending)) {
                dir = dir_pending;
            }

            r = snake_step();

            if (r == 2) {
                /* Choque: ruido de muerte + parpadeo de pausa antes del Game Over. */
                snd_die();
                dead = 1;
                game_over = 1;
            } else {
                if (r == 1) {
                    snd_eat();
                } else if (r == 3) {
                    snd_bonus();
                    game_over = 1;
                    dead = 0;
                } else {
                    snd_move();   /* tic al dar un paso normal */
                }
                hud_update_score();          /* PTS + BEST + NIV */
                put_u16_at(32, 27, snake_len, 3);
            }
        }

        snd_update();
        vc_wait_vblank_end();
    }

    /* Pausa de muerte: deja ver la colision parpadeando la cabeza. */
    if (dead) {
        uint8_t i;
        for (i = 0; i < DEATH_PAUSE && !quit_flag; i++) {
            vc_wait_vblank();
            read_input();
            snd_update();
            if (i & 2) {
                head_hide();
            } else {
                head_place(snake[0].x, snake[0].y, dir);
            }
            vc_wait_vblank_end();
        }
    }

    /* Asegura que la cabeza quede oculta al salir de la partida (el llamante
     * puede dibujar un panel encima y no queremos el sprite flotando). */
    head_hide();

    return quit_flag ? 0 : 1;   /* 1 = perdio o gano */
}

/* ===========================================================================
 * PANTALLA DE BIENVENIDA (banner)
 * ===========================================================================
 * Muestra el titulo y espera a que se pulse el BOTON (fuego del joystick, o
 * ESPACIO/ENTER por UART) para empezar. Devuelve 1 para jugar, 0 para salir.
 * =========================================================================== */

/* ===========================================================================
 * EDITOR DE CARA
 * ===========================================================================
 * Lienzo 8x8 = 8x8 celdas de pantalla. El cursor es un sprite con marco.
 * Se pinta con el boton de fuego, ciclando el color (1 cuerpo, 2 ojo, 3 pupila).
 * SALVAR escribe FACE.DAT por la ROM API (MicroFS) y recarga los patrones.
 *
 * Entrada:
 *   joystick : stick mueve, fuego pinta
 *   teclado  : WASD/flechas mueven, ESPACIO pinta, 1/2/3 eligen color,
 *              G guarda y sale, Q cancela y sale
 * =========================================================================== */

#define CANVAS_X   14
#define CANVAS_Y   8

/* Recuadro del color del cuerpo: a la DERECHA del lienzo de 8x8. El cursor
 * puede salir del lienzo hacia alla y se mueve libremente entre los dos. */
#define BODY_COL     26        /* columna del recuadro (a la derecha del 8x8) */
#define BODY_ROW     11        /* fila del recuadro                            */

/* Estado del editor. */
static uint8_t ed_cx, ed_cy;   /* cursor dentro del lienzo 0..7 */
static uint8_t ed_color;       /* 0, 1, 2, 4 (FC_*) */
static uint8_t ed_body;        /* 1 = el cursor esta en el selector de color */

/* Pinta una celda del lienzo segun su indice de color. */
static void canvas_cell(uint8_t r, uint8_t c) {
    uint8_t v = FACE_AT(r, c);
    uint8_t x = (uint8_t)(CANVAS_X + c);
    uint8_t y = (uint8_t)(CANVAS_Y + r);

    if (v == FC_TRANS) {
        /* Transparente: hueco real, se ve el negro del fondo como en el juego. */
        paint(x, y, TILE_BLANK, PAL_TEXT);
    } else if (v == FC_AZUL) {
        paint(x, y, TILE_SOL0, PAL_CANVAS);     /* color 1 = azul     */
    } else if (v == FC_AMARILLO) {
        paint(x, y, TILE_SOL1, PAL_CANVAS);     /* color 2 = amarillo */
    } else {
        paint(x, y, TILE_SOL2, PAL_CANVAS_W);   /* color 3 = blanco   */
    }
}

/* Redibuja el lienzo completo. */
static void canvas_draw_all(void) {
    uint8_t r, c;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            canvas_cell(r, c);
}

/* ---------------------------------------------------------------------------
 * SELECTOR DE COLOR DE CUERPO
 * --------------------------------------------------------------------------- */

/* Dibuja el recuadro del color del cuerpo, a la derecha del lienzo: un bloque
 * de 2x2 celdas con el color elegido. Usa TILE_SOL0, que pinta en el COLOR 1:
 * el mismo indice en que body_apply() guarda el color del cuerpo. */
#define BODY_W  2
#define BODY_H  2

static void body_sel_draw(void) {
    uint8_t i, j;
    for (i = 0; i < BODY_H; i++)
        for (j = 0; j < BODY_W; j++)
            paint((uint8_t)(BODY_COL + j), (uint8_t)(BODY_ROW + i),
                  TILE_SOL0, PAL_BODY);      /* color 1 = cuerpo */
    put_str_pal((uint8_t)(BODY_COL - 1), (uint8_t)(BODY_ROW - 1), "CUERPO", PAL_TEXT);
}

/* Coloca el sprite del cursor en su celda (o en el selector de color). */
static void cursor_place(void) {
    if (ed_body) {
        vc_sprite_move(SPR_EDIT_CUR,
                       (uint16_t)(BODY_COL * 8),
                       (uint8_t)(BODY_ROW * 8),
                       VC_SPPAL_1);
    } else {
        vc_sprite_move(SPR_EDIT_CUR,
                       (uint16_t)((CANVAS_X + ed_cx) * 8),
                       (uint8_t)((CANVAS_Y + ed_cy) * 8),
                       VC_SPPAL_1);
    }
    vc_oam_put(SPR_EDIT_CUR, VC_OAM_TILE, SPR_PAT_CUR);
}

/* Alterna el color del marco (blanco <-> negro) para que no se pierda sobre
 * las zonas blancas del dibujo. */
static void cursor_flash(uint8_t on) {
    vc_oam_put(SPR_EDIT_CUR, VC_OAM_FLAGS, (uint8_t)(on ? PAL_CUR_ON : PAL_CUR_OFF));
}

/* Texto fijo del editor + indicador de color. */
static void editor_hud(void) {
    char b[2];

    put_str_pal(11, 1, "DISENA TU CARA", PAL_TITLE);
    put_str_pal(1, 4,  "MOVER: stick", PAL_TEXT);
    put_str_pal(1, 5,  "PINTAR: fuego", PAL_TEXT);
    put_str_pal(1, 20, "COLOR:", PAL_TEXT);
    b[0] = (char)('0' + ed_color);
    b[1] = 0;
    put_str_pal(7, 20, b, PAL_TITLE);
    put_str_pal(1, 23, "G GUARDAR   Q CANCELAR", PAL_TEXT);
    put_str_pal(1, 25, "CUERPO:", PAL_TEXT);
}

/* Repinta el numero de color del HUD. */
static void editor_num(void) {
    char b[2];
    b[0] = (char)('0' + ed_color);
    b[1] = 0;
    put_str_pal(7, 20, b, PAL_TITLE);
}

/* Pinta la celda del cursor con el color actual y pasa al siguiente. */
static void editor_paint(void) {
    char b[2];

    FACE_AT(ed_cy, ed_cx) = ed_color;
    canvas_cell(ed_cy, ed_cx);

    /* Ciclo: 0 -> 1 -> 2 -> 4 -> 0 (el 3 no existe). */
    if      (ed_color == FC_TRANS)    ed_color = FC_AZUL;
    else if (ed_color == FC_AZUL)     ed_color = FC_AMARILLO;
    else if (ed_color == FC_AMARILLO) ed_color = FC_BLANCO;
    else                              ed_color = FC_TRANS;
    b[0] = (char)('0' + ed_color);
    b[1] = 0;
    put_str_pal(7, 20, b, PAL_TITLE);
}

/* Estado de repeticion del joystick en el editor (autorepeat).
 * El stick no repite en el mismo frame en que se pulsa: hay un retardo inicial
 * y luego repite cada REPEAT_DIV frames. Asi un toque mueve UNA celda y
 * mantener pulsado se desplaza de forma controlada. */
#define ED_REPEAT_DELAY  14   /* frames antes de empezar a repetir */
#define ED_REPEAT_DIV     5   /* repite cada 5 frames (el bucle va a 60 Hz) */
#define ED_FLASH_DIV     16   /* frames entre cambios de color del cursor (~9 Hz) */

/* ---------------------------------------------------------------------------
 * SELECTOR DE COLOR DE CUERPO
 * ---------------------------------------------------------------------------
 * Si el cursor baja hasta la ultima fila del lienzo (ed_body = 1),
 * izquierda/derecha recorren la lista de colores y el cambio se aplica al
 * instante en el tile del cuerpo y en la cabeza: lo que se ve es exactamente el
 * resultado final. El bloque y las flechas se dibujan en body_sel_draw(). */

static uint8_t ed_joy_prev;   /* estado previo del stick (flanco) */
static uint8_t ed_hold;       /* frames que lleva mantenido */
static uint8_t ed_flash;      /* contador del parpadeo del cursor */
static uint8_t ed_flash_on;   /* 1 = marco blanco, 0 = marco negro */

/* Una direccion en el editor: 0=izq 1=der 2=arriba 3=abajo.
 * El cursor se mueve libre entre el lienzo 8x8 y el recuadro del color del
 * cuerpo, que esta a su derecha: desde la ultima columna se sale al recuadro y
 * desde el recuadro, hacia la izquierda, se vuelve al lienzo. */
static void ed_move(uint8_t d) {
    if (ed_body) {                       /* en el recuadro del cuerpo */
        if (d == 0) ed_body = 0;         /* vuelve al lienzo (misma fila) */
        return;
    }
    if (d == 0)      { if (ed_cx) ed_cx--; }
    else if (d == 1) { if (ed_cx < 7) ed_cx++; else ed_body = 1; }
    else if (d == 2) { if (ed_cy) ed_cy--; }
    else             { if (ed_cy < 7) ed_cy++; }
}

/* Editor. Devuelve 1 = guardado, 0 = cancelado. */
static uint8_t face_editor(void) {
    uint8_t dirty = 1;

    ed_cx = 0; ed_cy = 0; ed_color = FC_AZUL;
    ed_body = 0;
    ed_joy_prev = joy_read();     /* parte del estado actual, sin evento fantasma */
    ed_hold = 0;
    ed_flash = 0;
    ed_flash_on = 1;

    vc_wait_vblank();
    clear_screen();
    editor_hud();
    cursor_place();
    vc_wait_vblank_end();

    for (;;) {
        uint8_t j, edge;
        char c;

        vc_wait_vblank();
        if (dirty) {
            canvas_draw_all();
            body_sel_draw();
            cursor_place();
            dirty = 0;
        }
        /* Parpadeo del cursor: cambia de color cada ED_FLASH_DIV frames. */
        if (++ed_flash == ED_FLASH_DIV) {
            ed_flash = 0;
            ed_flash_on = (uint8_t)!ed_flash_on;
        }
        cursor_flash(ed_flash_on);
        vc_wait_vblank_end();

        /* --- Joystick, con autorepeat --- */
        j    = joy_read();
        edge = (uint8_t)(j & (uint8_t)~ed_joy_prev);   /* recien pulsado */
        ed_joy_prev = j;

        /* Una "pulsacion efectiva" = flanco de direccion, o repeticion tras
         * mantener el stick el tiempo de espera. */
        if (j & (JOY_A_LEFT | JOY_A_RIGHT | JOY_A_UP | JOY_A_DOWN)) {
            ed_hold++;
        } else {
            ed_hold = 0;
        }

        if ((edge & (JOY_A_LEFT | JOY_A_RIGHT | JOY_A_UP | JOY_A_DOWN)) ||
            (ed_hold >= ED_REPEAT_DELAY && (ed_hold % ED_REPEAT_DIV) == 0)) {

            uint8_t d = 3;
            if (j & JOY_A_LEFT)       d = 0;
            else if (j & JOY_A_RIGHT) d = 1;
            else if (j & JOY_A_UP)    d = 2;
            ed_move(d);
            if (ed_body) body_apply();   /* paleta: dentro del VBLANK */
            cursor_place();
            dirty = 1;
        }

        /* Boton de fuego: pinta, o cambia el color del CUERPO si el cursor
         * esta en el recuadro. Solo en el FLANCO (una vez por pulsacion). */
        if (edge & JOY_A_FIRE) {
            if (ed_body) {
                face_buf[FACE_PAL] = (uint8_t)((face_buf[FACE_PAL] + 1) & 3);
                body_apply();          /* dentro del VBLANK */
                dirty = 1;
            } else {
                editor_paint();
                dirty = 1;
            }
        }

        /* --- Teclado (UART) --- */
        while (rom_uart_rx_ready()) {
            c = rom_uart_getc();
            switch (c) {
                case 'a': case 'A': ed_move(0); dirty = 1; if (ed_body) body_apply(); cursor_place(); break;
                case 'd': case 'D': ed_move(1); dirty = 1; if (ed_body) body_apply(); cursor_place(); break;
                case 'w': case 'W': ed_move(2); dirty = 1; cursor_place(); break;
                case 's': case 'x': ed_move(3); dirty = 1; cursor_place(); break;
                case ' ':          if (ed_body) {
                                       face_buf[FACE_PAL] = (uint8_t)((face_buf[FACE_PAL] + 1) & 3);
                                       body_apply(); dirty = 1;
                                   } else editor_paint();
                                   break;
                case '0': ed_color = FC_TRANS;    editor_num(); break;
                case '1': ed_color = FC_AZUL;     editor_num(); break;
                case '2': ed_color = FC_AMARILLO; editor_num(); break;
                case '4': ed_color = FC_BLANCO;   editor_num(); break;
                case 'g': case 'G':
                    if (face_save()) {
                        face_load_patterns();                    /* aplica al instante */
                        vc_sprite_disable(SPR_EDIT_CUR);
                        return 1;
                    }
                    put_str_pal(1, 27, "ERROR SD", PAL_FRUIT);  /* no sale: no pierde el dibujo */
                    break;
                case 'q': case 'Q':
                    vc_sprite_disable(SPR_EDIT_CUR);
                    return 0;
                default: break;
            }
        }
    }
}

/* Resultados de la pantalla de titulo. */
#define TITLE_JUGAR   0
#define TITLE_CARA    1
#define TITLE_SALIR   2

/* Lector de entrada del menu: devuelve UN evento por llamada.
 * Reutiliza joystick y teclado igual que el juego. */
#define EV_NONE   0
#define EV_UP     1
#define EV_DOWN   2
#define EV_FIRE   3
#define EV_QUIT   4

/* Estado previo del joystick, para detectar flancos (anti-rebote). */
static uint8_t ev_joy_prev;
/* Reinicia el anti-rebote (llamar al entrar a una pantalla con menu). */
static void ev_reset(void) {
    ev_joy_prev = joy_read();
}

/* Descarta lo que quede en la FIFO del UART. Se usa al cambiar de pantalla
 * para que una tecla pulsada de mas no se interprete en la pantalla siguiente
 * (p. ej. la 'r' del GAME OVER entrando como "JUGAR" en el menu). */
static void ev_flush(void) {
    uint8_t guard = 0;
    while (rom_uart_rx_ready() && guard < 64) {
        (void)rom_uart_getc();
        guard++;
    }
    ev_joy_prev = joy_read();
}

static uint8_t ev_read(void) {
    uint8_t j;
    uint8_t edge;
    char c;

    j    = joy_read();
    edge = (uint8_t)(j & (uint8_t)~ev_joy_prev);   /* bits recien pulsados */
    ev_joy_prev = j;

    if (edge & JOY_A_FIRE) return EV_FIRE;
    if (edge & JOY_A_UP)   return EV_UP;
    if (edge & JOY_A_DOWN) return EV_DOWN;

    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();
        switch (c) {
            case 'w': case 'W': case '8': return EV_UP;
            case 's': case 'S': case '2': return EV_DOWN;
            case ' ': case '\r': case '\n': return EV_FIRE;
            case 'q': case 'Q': return EV_QUIT;
            default: break;
        }
    }
    return EV_NONE;
}

/* Pinta la lista de opciones, resaltando la seleccionada. */
static void title_menu_draw(uint8_t sel) {
    uint8_t i;

    for (i = 0; i < 3; i++) {
        uint8_t row = (uint8_t)(17 + i * 2);   /* una fila de aire entre opciones */
        uint8_t pal = (i == sel) ? PAL_TITLE : PAL_TEXT;
        char mark[2];

        mark[0] = (i == sel) ? '>' : ' ';
        mark[1] = 0;
        put_str_pal(13, row, mark, pal);

        if (i == 0)      put_str_pal(15, row, "JUGAR",      pal);
        else if (i == 1) put_str_pal(15, row, "CREAR CARA", pal);
        else             put_str_pal(15, row, "SALIR",      pal);
    }
}

/* Pantalla de titulo / menu. Devuelve TITLE_* */
static uint8_t title_screen(void) {
    uint8_t sel = 0;
    uint8_t need_redraw = 1;

    ev_flush();          /* no hereda teclas de la pantalla anterior */
    vc_wait_vblank();

    /* Fondo negro en toda la pantalla. */
    clear_screen();

    /* Titulo (letras separadas) en verde. */
    put_str_pal(15, 6, "S N A K E", PAL_TITLE);

    /* Logo: la cabeza de la serpiente (sprite) entre dos frutas. */
    paint(12, 10, TILE_FRUIT_FRESA, PAL_FRUIT);
    paint(27, 10, TILE_FRUIT_MANZANA, PAL_FRUIT);
    head_place(19, 10, DIR_RIGHT);

    /* Ayuda. */
    put_str_pal(8, 13, "COME LA FRUTA Y CRECE", PAL_TEXT);

    vc_wait_vblank_end();

    ev_reset();     /* anti-rebote: ignora lo que este pulsado ahora mismo */

    /* Espera de gracia: al volver del GAME OVER viene una tecla en camino
     * ('r' o el borde del boton). Se descarta durante unos frames para que no
     * entre al menu como "JUGAR" y reinicie la partida sin querer. */
    {
        uint8_t d;
        for (d = 0; d < 30; d++) {
            vc_wait_vblank();
            ev_flush();
            vc_wait_vblank_end();
        }
    }

    for (;;) {
        vc_wait_vblank();

        if (need_redraw) {
            title_menu_draw(sel);
            head_place(19, 10, DIR_RIGHT);   /* el sprite se pierde si no se refresca */
            need_redraw = 0;
        }

        vc_wait_vblank_end();

        switch (ev_read()) {
            case EV_UP:
                if (sel > 0) { sel--; need_redraw = 1; }
                break;
            case EV_DOWN:
                if (sel < 2) { sel++; need_redraw = 1; }
                break;
            case EV_FIRE:
                head_hide();
                return sel;          /* 0=JUGAR 1=CARA 2=SALIR */
            case EV_QUIT:
                head_hide();
                return TITLE_SALIR;
            default:
                break;
        }
    }
}

/* ===========================================================================
 * DESPEDIDA (pantalla al salir con 'q')
 * ===========================================================================
 * Limpia la pantalla y dibuja un gusanito (cuerpo + cabeza) junto a un "BYE!",
 * deja un instante para verlo y vuelve al monitor. */
static void bye_screen(void) {
    uint8_t x, y, i;
    /* Fila/columna del homenaje. El gusano mira a la DERECHA, con el morro
     * hacia el texto. El conjunto (gusano + "BYE!") queda centrado. */
    const uint8_t row = 14;
    const uint8_t x0  = 14;   /* columna del cuerpo (izquierda) */

    vc_wait_vblank();

    /* Fondo limpio. */
    for (y = 0; y < VC_SCREEN_ROWS; y++)
        for (x = 0; x < VC_SCREEN_COLS; x++)
            paint(x, y, TILE_BLANK, PAL_TEXT);

    /* Cuerpo del gusano: una fila de tiles; la celda de la cabeza queda VACIA
     * (la ocupa el sprite, que se refresca en el bucle de abajo). */
    for (i = 0; i < 4; i++) {
        paint((uint8_t)(x0 + i), row, TILE_BODY, PAL_BODY);
    }

    /* Texto a la derecha del morro. */
    put_str_pal((uint8_t)(x0 + 7), row, "BYE!", PAL_TITLE);

    /* Globito de despedida (centrado). */
    put_str_pal(11, 10, "GRACIAS POR JUGAR", PAL_TEXT);

    vc_wait_vblank_end();

    /* Deja la pantalla visible un momento (~80 frames, ~1.3 s). La OAM se
     * reescribe cada frame (dentro del VBLANK) para que el sprite no se pierda.
     * Se descarta lo que llegue por UART mientras tanto: no debe quedar nada
     * pendiente para el programa que arranque tras el reset. */
    for (i = 0; i < 80; i++) {
        vc_wait_vblank();
        head_place((uint8_t)(x0 + 4), row, DIR_RIGHT);
        vc_wait_vblank_end();

        while (rom_uart_rx_ready()) (void)rom_uart_getc();
    }

    /* Apaga el sprite antes de volver al monitor. */
    head_hide();
}

/* ===========================================================================
 * SALIDA
 * ===========================================================================
 * Al terminar, se vuelve al monitor PIDIENDO AUTOBOOT: se deja el flag de
 * usuario del FPGA ($C004 bit 0 = 0) y se reinicia por el vector RESET, para
 * que el monitor relea BOOT.INI y lance el programa de arranque (launcher).
 *
 *   $C004 bit 0 = 1 -> monitor sin autoboot (prompt)
 *   $C004 bit 0 = 0 -> monitor CON autoboot (lee BOOT.INI)
 *
 * Es un registro de hardware fuera del dominio de reset: sobrevive al reinicio.
 * IMPORTANTE: usar `jmp ($FFFC)` (vector RESET) y no `jmp $8000`, porque solo el
 * primero hace que el monitor evalue el flag recien escrito.
 * =========================================================================== */
#define SYSFLAGS (*((volatile uint8_t *)0xC004))

/* Vuelve al monitor con autoboot (lanza BOOT.INI). No retorna. */
static void exit_with_autoboot(void) {
    uint8_t guard;

    snd_silence();
    vc_wait_vblank();
    vc_clear_oam();
    vc_set_scroll_x(0);
    vc_set_scroll_y(0);

    /* Vacia el buffer de la UART. Si quedan caracteres pendientes (p. ej. las
     * teclas pulsadas durante la despedida), el monitor/lanzador que arranca
     * tras el reset los consumiria y se saldria solo. Se descartan aqui. */
    guard = 0;
    while (rom_uart_rx_ready() && guard < 64) {
        (void)rom_uart_getc();
        guard++;
    }

    rom_uart_puts("\r\nReiniciando con autoboot (BOOT.INI)...\r\n");

    SYSFLAGS = 0x00;        /* permitir autoboot en el proximo reset */
    __asm__("jmp ($FFFC)"); /* reset por el vector -> monitor -> BOOT.INI */
}

/* ===========================================================================
 * MAIN
 * =========================================================================== */
int main(void) {
    rom_uart_puts("\r\nSNAKE - Core de Video\r\n");
    rom_uart_puts("Joystick o WASD/flechas = mover, q = salir.\r\n");

    setup_video();
    snd_init();
    joy_init();     /* bits 3-7 del Puerto 1 como entrada (joystick) */
    hs_reset();     /* tabla de records vacia (dura toda la sesion) */

    /* Cara: de FACE.DAT si existe y es valida, si no la de fabrica. */
    face_load();
    body_apply();      /* el color del cuerpo segun FACE.DAT */
    face_load_patterns();

    /* Bucle principal: banner -> jugar / editar cara / salir. Tras cada
     * partida (ganada o perdida) se vuelve aqui, al banner. */
    for (;;) {
        uint8_t r = title_screen();

        if (r == TITLE_SALIR) break;
        if (r == TITLE_CARA) {
            face_editor();
            continue;
        }

        if (!play_game()) {
            /* El jugador pulso 'q' durante la partida: al monitor. */
            break;
        }

        snd_silence();
        vc_wait_vblank();

        /* Inserta la puntuacion y el largo alcanzado en el top 5. */
        hs_insert(score, snake_len);

        /* Limpia toda la pantalla para el panel de fin de partida. */
        clear_screen();

        /* Panel de GAME OVER (centrado; deja sitio al top 5 debajo). */
        put_str_pal(15, 3, "GAME OVER", PAL_TITLE);

        /* PUNTOS centrado: "PUNTOS:" (7) + espacio + 4 digitos = 12 cols. */
        put_str_at(14, 6, "PUNTOS:");
        put_u16_at(22, 6, score, 4);

        /* Tabla top 5: 12 columnas -> centrada en col 14. */
        hs_draw(14, 9);

        /* Instrucciones (centradas, con una fila de aire). */
        put_str_at(13, 18, "BOTON = menu");
        put_str_at(16, 19, "Q = salir");

        clear_str_at(0, 27, 40);
        vc_wait_vblank_end();
        rom_uart_puts("\r\nGAME OVER - BOTON = volver al menu, Q = salir\r\n");

        /* Pequeña espera de cortesia: evita volver al menu al instante si
         * el boton se quedo pulsado al morir. */
        {
            uint8_t d;
            for (d = 0; d < 20; d++) {
                vc_wait_vblank();
                vc_wait_vblank_end();
            }
        }

        for (;;) {
            char c;

            /* Boton del joystick = volver al menu (no reiniciar). */
            if (joy_read() & JOY_A_FIRE) {
                ev_flush();
                rom_uart_puts("\r\nVolviendo al menu.\r\n");
                break;
            }

            if (!rom_uart_rx_ready()) {
                vc_wait_vblank();
                vc_wait_vblank_end();
                continue;
            }
            c = rom_uart_getc();
            if (c == 'r' || c == 'R') {
                ev_flush();   /* que la tecla no entre en el menu */
                rom_uart_puts("\r\nVolviendo al menu.\r\n");
                break;
            }
            if (c == 'q' || c == 'Q') goto salir;
        }
    }

salir:
    /* Salida: pantalla de despedida y vuelta al monitor CON autoboot. */
    bye_screen();
    exit_with_autoboot();   /* no retorna */

    return 0;
}
