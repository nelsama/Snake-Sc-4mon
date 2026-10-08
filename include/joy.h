/**
 * ============================================================================
 * joy.h - Driver del joystick Atari (DB9) via GPIO del FPGA
 * ============================================================================
 * Lee el joystick en PARALELO al teclado UART. El juego decide que hacer con
 * las acciones devueltas; cambiar de fuente de entrada solo afecta a read_input.
 *
 * Hardware asumido (AJUSTAR si tu placa difiere):
 *   - Puerto 1 del FPGA.
 *   - $C000 = datos (lectura = entradas).
 *   - $C002 = config por bit (0 = salida, 1 = entrada).
 *   - Bits libres para el joystick: 3-7 (los 0-2 los ocupa el TM1638).
 *
 * El joystick Atari es activo por nivel BAJO (0 = pulsado).
 * ============================================================================
 */

#ifndef JOY_H
#define JOY_H

#include <stdint.h>

/* Bits del joystick en el Puerto 1 (Ajustar si tu mapeo cambia). */
#define JOY_RIGHT   0x08   /* bit 3 */
#define JOY_LEFT    0x10   /* bit 4 */
#define JOY_DOWN    0x20   /* bit 5 */
#define JOY_UP      0x40   /* bit 6 */
#define JOY_FIRE    0x80   /* bit 7 */
#define JOY_MASK    0xF8   /* los 5 bits del joystick (ignora el TM1638) */

/* El joystick Atari es activo por nivel bajo. Pon 0 si tu placa lee al reves. */
#define JOY_ACTIVE_LOW  1

/* Acciones devueltas por joy_read() (bitmask). */
#define JOY_A_LEFT   0x01
#define JOY_A_RIGHT  0x02
#define JOY_A_UP     0x04
#define JOY_A_DOWN   0x08
#define JOY_A_FIRE   0x10

/* Configura los bits 3-7 del Puerto 1 como entrada (read-modify-write:
 * preserva los bits 0-2 del TM1638). Llamar UNA vez al arrancar. */
void    joy_init(void);

/* Devuelve un bitmask de acciones JOY_A_* (0 = nada pulsado). */
uint8_t joy_read(void);

#endif /* JOY_H */
