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

/* Longitud maxima de la serpiente = celdas del tablero. */
#define MAX_SNAKE     (BOARD_COLS * BOARD_HEIGHT)

/* ===========================================================================
 * PATRONES
 * ===========================================================================
 * color del pixel = (plano1 << 1) | plano0   (indices 0..3, 0 = transparente).
 *
 * Los dibujos vienen de un sprite a color (formato Piskel, ARGB). Se mapean a
 * indices de paleta:
 *     0 = transparente (0x00000000)
 *     1 = cuerpo       (0xFF00FFF3)
 *     2 = blanco       (0xFFFFFFFF)  -> ojo
 *     3 = negro        (0xFF000000)  -> pupila
 *
 * La CABEZA se dibuja como SPRITE (OAM) y tiene 4 patrones, uno por direccion
 * (derecha / izquierda / arriba / abajo), sin depender de flips. El CUERPO y el
 * MURO son tiles de fondo (patrones en $0F y $11).
 * =========================================================================== */

/* Tile de fondo vacio: indice 0 -> transparente -> se ve BG_COLOR (negro). */
static const uint8_t tile_blank_p0[8] = { 0,0,0,0,0,0,0,0 };

/* Muro del tablero: bloque solido (indice 3), patron propio en $0F. */
static const uint8_t tile_wall_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

/* Cuerpo de la serpiente (simetrico), tile de fondo en $11. */
static const uint8_t tile_body_p0[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t tile_body_p1[8] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };

/* Cabeza de la serpiente, como PATRONES DE SPRITE (4 direcciones). */
/* Derecha (original). */
static const uint8_t spr_head_r_p0[8] = { 0x3E,0xF1,0xE5,0xF1,0xF1,0xE5,0xF1,0x3E };
static const uint8_t spr_head_r_p1[8] = { 0x00,0x0E,0x1E,0x0E,0x0E,0x1E,0x0E,0x00 };
/* Arriba. */
static const uint8_t spr_head_u_p0[8] = { 0x7E,0x81,0xA5,0x81,0xDB,0xFF,0x7E,0x7E };
static const uint8_t spr_head_u_p1[8] = { 0x00,0x7E,0x7E,0x7E,0x24,0x00,0x00,0x00 };

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

/* Sprite de la cabeza. */
#define SPR_HEAD      0      /* slot de OAM de la cabeza                      */
#define SPR_PAT_HEAD_R 0     /* patron de sprite: mira a la DERECHA           */
#define SPR_PAT_HEAD_L 1     /* patron de sprite: mira a la IZQUIERDA         */
#define SPR_PAT_HEAD_U 2     /* patron de sprite: mira ARRIBA                 */
#define SPR_PAT_HEAD_D 3     /* patron de sprite: mira ABAJO                  */

/* Paletas. La cabeza es sprite -> usa una paleta de SPRITE (VC_SPPAL_*).
 * El cuerpo es tile -> usa una paleta de FONDO (VC_BGPAL_*). */
#define PAL_WALL     VC_BGPAL_0   /* preset: color 3 = blanco      */
#define PAL_BODY     VC_BGPAL_0   /* el cuerpo usa su propio indice (ver setup) */
#define PAL_SNAKE_S  VC_SPPAL_2   /* paleta de sprite de la cabeza */
#define PAL_FRUIT    VC_BGPAL_1   /* paleta de las frutas: 1=rojo, 2=verde, 3=negro */
#define PAL_TEXT     VC_BGPAL_0   /* tinta de texto blanca         */
#define PAL_TITLE    VC_BGPAL_2   /* tinta del banner (verde)      */
/* NOTA: la paleta 3 NO se usa para texto: su color 3 es BG_COLOR (entrada 15),
 * asi que reescribirla cambiaria el color de fondo. Los subtitulos van en blanco. */

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

/* Mejores puntuaciones de la sesion (top 5), en orden descendente.
 * Viven en RAM: se pierden al salir al monitor (jmp $8000 reinicia la RAM). */
#define HS_COUNT  5
static uint16_t high_scores[HS_COUNT];
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
    for (i = 0; i < HS_COUNT; i++) high_scores[i] = 0;
    hs_last_idx = 0xFF;
}

/* Inserta 'v' en la tabla (orden descendente) y desplaza el resto. Guarda en
 * hs_last_idx la posicion final (o 0xFF si no entro en el top 5). No inserta
 * puntuaciones de 0. */
static void hs_insert(uint16_t v) {
    uint8_t i, pos;

    hs_last_idx = 0xFF;
    if (v == 0) return;   /* no guardamos partidas de 0 puntos */

    for (pos = 0; pos < HS_COUNT; pos++) {
        if (v > high_scores[pos]) break;
    }
    if (pos >= HS_COUNT) return;   /* no entra en el top 5 */

    /* Desplaza hacia abajo desde el final hasta 'pos'. */
    for (i = (uint8_t)(HS_COUNT - 1); i > pos; i--) {
        high_scores[i] = high_scores[i - 1];
    }
    high_scores[pos] = v;
    hs_last_idx = pos;
}

/* Dibuja la tabla de records en la columna 'col', empezando en la fila 'row'. */
static void hs_draw(uint8_t col, uint8_t row) {
    uint8_t i;

    put_str_pal(col, row, "MEJORES:", PAL_TITLE);
    for (i = 0; i < HS_COUNT; i++) {
        uint8_t r = (uint8_t)(row + 1 + i);
        uint8_t pal = (i == hs_last_idx) ? PAL_TITLE : PAL_TEXT;

        /* "1. 0100" */
        char buf[3];
        buf[0] = (char)('1' + i);
        buf[1] = '.';
        buf[2] = 0;
        put_str_pal(col, r, buf, pal);
        put_u16_pal((uint8_t)(col + 3), r, high_scores[i], 4, pal);
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
    vc_pal_set_bg(PAL_BODY,  1, VC_RGB444(0x0, 0x0, 0xA));  /* azul oscuro (cuerpo) */
    /* Paleta de las frutas (3 colores): 1 = rojo, 2 = verde, 3 = negro. */
    vc_pal_set_bg(PAL_FRUIT, 1, VC_RGB444(0xF, 0x0, 0x0));  /* rojo  */
    vc_pal_set_bg(PAL_FRUIT, 2, VC_RGB444(0x0, 0xF, 0x0));  /* verde */
    vc_pal_set_bg(PAL_FRUIT, 3, VC_RGB444(0x0, 0x0, 0x0));  /* negro */
    /* Paletas del banner: solo se usa el COLOR 3 (tinta del texto de la fuente).
     * OJO: la paleta 3 NO se toca, porque su color 3 es la entrada 15 = BG_COLOR
     * (comparten entrada); reescribirla cambiaria el color de fondo. */
    vc_pal_set_bg(PAL_TITLE, 3, VC_RGB444(0x0, 0xF, 0x4));  /* verde brillante */

    /* Reafirma el fondo NEGRO al final (entrada 15), por si algo la toco. */
    vc_set_bgcolor(VC_RGB444(0x0, 0x0, 0x0));
    vc_pal_set_spr(PAL_SNAKE_S, 1, VC_RGB444(0x0, 0x0, 0xA)); /* azul oscuro (cuerpo) */
    vc_pal_set_spr(PAL_SNAKE_S, 2, VC_RGB444(0xF, 0xF, 0xF)); /* blanco (ojo)         */
    vc_pal_set_spr(PAL_SNAKE_S, 3, VC_RGB444(0x0, 0x0, 0x0)); /* negro  (pupila)      */

    /* Patrones: muro, cuerpo y frutas (tiles de fondo) + cabeza (sprites).
     * Solo hay dibujo explicito para DERECHA y ARRIBA; IZQUIERDA y ABAJO se
     * obtienen por flip en tiempo de dibujo (head_place). */
    vc_load_bg_pattern(TILE_WALL, tile_wall_p0, tile_wall_p0);
    vc_load_bg_pattern(TILE_BODY, tile_body_p0, tile_body_p1);
    vc_load_bg_pattern(TILE_FRUIT_FRESA,   tile_fruit_straw_p0, tile_fruit_straw_p1);
    vc_load_bg_pattern(TILE_FRUIT_MANZANA, tile_fruit_apple_p0, tile_fruit_apple_p1);
    vc_load_spr_pattern(SPR_PAT_HEAD_R, spr_head_r_p0, spr_head_r_p1);
    vc_load_spr_pattern(SPR_PAT_HEAD_U, spr_head_u_p0, spr_head_u_p1);

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

    return quit_flag ? 0 : 1;   /* 1 = perdio o gano */
}

/* ===========================================================================
 * PANTALLA DE BIENVENIDA (banner)
 * ===========================================================================
 * Muestra el titulo y espera a que se pulse el BOTON (fuego del joystick, o
 * ESPACIO/ENTER por UART) para empezar. Devuelve 1 para jugar, 0 para salir.
 * =========================================================================== */

/* 1 si se pide empezar (boton de fuego o ESPACIO/ENTER). */
static uint8_t start_pressed(void) {
    char c;

    if (joy_read() & JOY_A_FIRE) return 1;

    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();
        if (c == ' ' || c == '\r' || c == '\n' || c == 'e' || c == 'E') return 1;
        if (c == 'q' || c == 'Q') return 2;   /* salir */
    }
    return 0;
}

/* Pantalla de titulo. Devuelve 1 = jugar, 0 = salir. */
static uint8_t title_screen(void) {
    vc_wait_vblank();

    /* Fondo negro en toda la pantalla. */
    {
        uint8_t x, y;
        for (y = 0; y < VC_SCREEN_ROWS; y++)
            for (x = 0; x < VC_SCREEN_COLS; x++)
                paint(x, y, TILE_BLANK, PAL_TEXT);
    }

    /* Titulo (letras separadas) en verde. */
    put_str_pal(14, 8, "S N A K E", PAL_TITLE);

    /* Logo: la cabeza de la serpiente (sprite) entre dos frutas. */
    paint(12, 11, TILE_FRUIT_FRESA, PAL_FRUIT);
    paint(27, 11, TILE_FRUIT_MANZANA, PAL_FRUIT);
    head_place(19, 11, DIR_RIGHT);

    /* Ayuda (tinta blanca, paleta 0). */
    put_str_pal(9, 15, "COME LA FRUTA Y CRECE", PAL_TEXT);
    put_str_pal(7, 17, "JOYSTICK o WASD/flechas", PAL_TEXT);

    vc_wait_vblank_end();

    /* Espera al boton. Parpadeo estilo arcade clasico: el texto se enciende y
     * se apaga a ~1.5 Hz (periodo ~40 frames por estado), y SOLO se escribe en
     * VRAM cuando cambia el estado (no cada frame) para evitar centelleo. */
    {
        uint8_t  shown = 0xFF;  /* 0xFF = aun no dibujado (fuerza el 1er dibujo) */
        uint16_t hold  = 0;     /* frames que lleva el estado actual */

        for (;;) {
            uint8_t r;
            uint8_t on = (uint8_t)((hold / 40) & 1);   /* 0 = visible, 1 = oculto */

            vc_wait_vblank();

            /* Redibuja SOLO en el cambio de estado (on != shown). */
            if (on != shown) {
                if (on) {
                    clear_str_at(11, 21, 11);       /* parpadeo: borra el texto */
                } else {
                    put_str_pal(11, 21, "PULSA BOTON", PAL_TITLE);
                }
                shown = on;
            }
            hold++;
            vc_wait_vblank_end();

            r = start_pressed();
            if (r == 1) {
                head_hide();
                return 1;
            }
            if (r == 2) {
                head_hide();
                return 0;
            }
        }
    }
}

/* ===========================================================================
 * MAIN
 * =========================================================================== */
int main(void) {
    uint8_t again;

    rom_uart_puts("\r\nSNAKE - Core de Video\r\n");
    rom_uart_puts("Joystick o WASD/flechas = mover, q = salir.\r\n");

    setup_video();
    snd_init();
    joy_init();     /* bits 3-7 del Puerto 1 como entrada (joystick) */
    hs_reset();     /* tabla de records vacia (dura toda la sesion) */

    /* Pantalla de bienvenida: espera al boton antes de empezar. */
    if (!title_screen()) {
        snd_silence();
        vc_wait_vblank();
        vc_clear_oam();
        vc_set_scroll_x(0);
        vc_set_scroll_y(0);
        rom_uart_puts("\r\nSaliendo al monitor...\r\n");
        return 0;
    }

    do {
        again = 0;

        if (play_game()) {
            uint8_t x, y;

            snd_silence();
            vc_wait_vblank();

            /* Inserta la puntuacion en el top 5 (guarda hs_last_idx). */
            hs_insert(score);

            /* Limpia toda la pantalla para el panel de fin de partida. */
            for (y = 0; y < VC_SCREEN_ROWS; y++) {
                for (x = 0; x < VC_SCREEN_COLS; x++) {
                    paint(x, y, TILE_BLANK, PAL_TEXT);
                }
            }

            /* Panel de GAME OVER (todo mas arriba para dejar sitio al top 5). */
            put_str_pal(16, 3, "GAME OVER", PAL_TITLE);

            put_str_at(13, 6, "PUNTOS:");
            put_u16_at(21, 6, score, 4);

            hs_draw(13, 8);   /* "MEJORES:" + 5 filas (filas 8..13) */

            put_str_at(9, 16, "BOTON/R = reiniciar");
            put_str_at(9, 17, "Q = salir");

            clear_str_at(0, 27, 40);
            vc_wait_vblank_end();
            rom_uart_puts("\r\nGAME OVER - BOTON/R = jugar de nuevo, Q = salir\r\n");

            /* Pequeña espera de cortesia: evita reiniciar al instante si el
             * boton se quedo pulsado al morir. */
            {
                uint8_t d;
                for (d = 0; d < 20; d++) {
                    vc_wait_vblank();
                    vc_wait_vblank_end();
                }
            }

            for (;;) {
                char c;

                /* Boton del joystick = jugar de nuevo. */
                if (joy_read() & JOY_A_FIRE) { again = 1; break; }

                if (!rom_uart_rx_ready()) {
                    vc_wait_vblank();
                    vc_wait_vblank_end();
                    continue;
                }
                c = rom_uart_getc();
                if (c == 'r' || c == 'R') { again = 1; break; }
                if (c == 'q' || c == 'Q') { again = 0; break; }
            }

            if (again) {
                /* play_game() -> snake_reset() -> board_draw() limpia la pantalla;
                 * no hace falta borrar el panel a mano. */
                rom_uart_puts("\r\nNueva partida.\r\n");
            }
        }
    } while (again);

    /* Salida limpia al monitor. */
    snd_silence();
    vc_wait_vblank();
    vc_clear_oam();
    vc_set_scroll_x(0);
    vc_set_scroll_y(0);
    rom_uart_puts("\r\nSaliendo al monitor...\r\n");
    return 0;
}
